// Scans ~/.claude/projects for live Claude Code sessions and derives the
// data the firmware renders: name, status, and "ms since last activity".

import { readdir, readFile, stat } from "node:fs/promises";
import { homedir } from "node:os";
import { basename, join } from "node:path";

export type SessionStatus =
  | "working"
  | "waiting"
  | "idle"
  | "done"
  | "error";

export interface SessionSnapshot {
  /** Short label shown on the device (max ~14 chars after truncation). */
  n: string;
  /** Derived status. */
  st: SessionStatus;
  /** Milliseconds since this session's most recent activity. */
  ago: number;
}

interface JsonlEvent {
  type?: string;
  message?: { stop_reason?: string; role?: string };
  aiTitle?: string;
  isError?: boolean;
}

const PROJECTS_DIR = join(homedir(), ".claude", "projects");

// Walk back through the last N events of a file to find the most recent
// "real" assistant turn / user message. Everything in between (ai-title,
// agent-name, permission-mode, file-history-snapshot) is metadata.
const REAL_TYPES = new Set([
  "assistant",
  "user",
  "system",
  "attachment",
]);

const TAIL_LINES_TO_SCAN = 40;

// Sessions with no file activity for this long are dropped from the
// snapshot entirely — they'd just clutter the firmware's "+N inactive"
// counter without telling the user anything useful.
const MAX_AGE_MS = 60 * 60 * 1000;  // 1 hour

// A session file touched within this window is treated as ongoing.
// Authoritative — overrides whatever the last event happens to look like.
const ONGOING_MS = 30 * 1000;

interface DerivedState {
  status: SessionStatus;
  aiTitle: string | null;
}

function deriveState(events: JsonlEvent[]): DerivedState {
  let aiTitle: string | null = null;

  // First pass: pick up the most recent ai-title (it gets updated as the
  // model better understands the session).
  for (let i = events.length - 1; i >= 0; i--) {
    const evt = events[i];
    if (evt?.type === "ai-title" && typeof evt.aiTitle === "string") {
      aiTitle = evt.aiTitle;
      break;
    }
  }

  // Second pass: find the last "real" event and any error in the recent tail.
  let last: JsonlEvent | null = null;
  let sawError = false;
  for (let i = events.length - 1; i >= 0; i--) {
    const evt = events[i];
    if (!evt?.type) continue;
    if (evt.isError) sawError = true;
    if (REAL_TYPES.has(evt.type) && last === null) last = evt;
  }

  if (sawError) return { status: "error", aiTitle };
  if (last === null) return { status: "idle", aiTitle };

  if (last.type === "user") {
    // User just sent something; assistant turn presumably in flight.
    return { status: "working", aiTitle };
  }
  if (last.type === "assistant") {
    const stop = last.message?.stop_reason;
    if (stop === "tool_use") return { status: "working", aiTitle };
    if (stop === "end_turn" || stop === "stop_sequence") return { status: "waiting", aiTitle };
    return { status: "idle", aiTitle };
  }
  return { status: "idle", aiTitle };
}

function projectLabel(projectDir: string): string {
  // ~/.claude/projects/-Users-jvidal-code-claudewave → "claudewave"
  // ~/.claude/projects/-Users-jvidal-code-spearbit-clarion → "clarion"
  const slug = basename(projectDir);
  const parts = slug.replace(/^-/, "").split("-");
  return parts[parts.length - 1] ?? slug;
}

async function readTailEvents(file: string): Promise<JsonlEvent[]> {
  // Cheap-and-cheerful: read whole file, take last N lines. For our scale
  // (a few MB per session at most) the cost is negligible vs the polling
  // cadence; we can switch to a seek-to-end scan if files get huge.
  const text = await readFile(file, "utf8");
  const lines = text.split("\n");
  const tail = lines.slice(-TAIL_LINES_TO_SCAN);
  const events: JsonlEvent[] = [];
  for (const line of tail) {
    if (!line) continue;
    try {
      events.push(JSON.parse(line) as JsonlEvent);
    } catch {
      /* skip malformed lines */
    }
  }
  return events;
}

async function snapshotOne(
  projectDir: string,
  sessionFile: string,
): Promise<SessionSnapshot | null> {
  const path = join(projectDir, sessionFile);
  let mtimeMs: number;
  try {
    const s = await stat(path);
    if (!s.isFile()) return null;
    mtimeMs = s.mtimeMs;
  } catch {
    return null;
  }

  const ago = Math.max(0, Math.floor(Date.now() - mtimeMs));
  if (ago > MAX_AGE_MS) return null;

  const events = await readTailEvents(path);
  const derived = deriveState(events);

  // Authoritative ongoing signal: file touched in the last ONGOING_MS.
  // Overrides the parsed event status — the session file gets a new line
  // on every assistant→tool→user round-trip, so fresh mtime ≡ actively
  // doing something. Only fall back to event-derived status (waiting /
  // done / error / idle) once the file has been quiet for longer.
  const status: SessionStatus =
    ago <= ONGOING_MS ? "working" : derived.status;

  const name = derived.aiTitle ?? projectLabel(projectDir);

  return { n: name, st: status, ago };
}

export async function scanSessions(): Promise<SessionSnapshot[]> {
  let projects: string[];
  try {
    projects = await readdir(PROJECTS_DIR);
  } catch {
    return [];
  }

  const out: SessionSnapshot[] = [];
  for (const project of projects) {
    const projectDir = join(PROJECTS_DIR, project);
    let entries: string[];
    try {
      entries = await readdir(projectDir);
    } catch {
      continue;
    }
    for (const entry of entries) {
      if (!entry.endsWith(".jsonl")) continue;
      const snap = await snapshotOne(projectDir, entry);
      if (snap) out.push(snap);
    }
  }

  // Sort most-recent-first so the firmware's own sort is essentially a no-op.
  out.sort((a, b) => a.ago - b.ago);
  return out;
}
