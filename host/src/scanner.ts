// Scans ~/.claude/projects for live Claude Code sessions.
//
// Status model is intentionally minimal: active vs inactive. A session is
// active if its `.jsonl` file was touched in the last ONGOING_MS — the
// file gets a new line on every assistant turn / tool round-trip, so
// fresh mtime ≡ actively doing something. Everything else is inactive.
// The richer states (waiting / done / error) will come back when we have
// reliable signals for them.

import { readdir, readFile, stat } from "node:fs/promises";
import { homedir } from "node:os";
import { basename, join } from "node:path";

export type SessionStatus = "working" | "idle";

export interface SessionSnapshot {
  /** Short label shown on the device. */
  n: string;
  st: SessionStatus;
  /** Milliseconds since this session's most recent activity. */
  ago: number;
}

const PROJECTS_DIR = join(homedir(), ".claude", "projects");

// Sessions with no file activity for this long are dropped entirely.
const MAX_AGE_MS = 60 * 60 * 1000;  // 1 hour
const ONGOING_MS = 30 * 1000;

// Look back this many lines in each session file to find the most recent
// aiTitle (it gets updated as the model better understands the session).
const TAIL_LINES_TO_SCAN = 40;

async function readAiTitle(file: string): Promise<string | null> {
  const text = await readFile(file, "utf8");
  const lines = text.split("\n").slice(-TAIL_LINES_TO_SCAN);
  for (let i = lines.length - 1; i >= 0; i--) {
    const line = lines[i];
    if (!line) continue;
    try {
      const evt = JSON.parse(line) as { type?: string; aiTitle?: string };
      if (evt.type === "ai-title" && typeof evt.aiTitle === "string") {
        return evt.aiTitle;
      }
    } catch {
      /* skip malformed */
    }
  }
  return null;
}

function projectLabel(projectDir: string): string {
  // ~/.claude/projects/-Users-jvidal-code-claudewave → "claudewave"
  const slug = basename(projectDir);
  const parts = slug.replace(/^-/, "").split("-");
  return parts[parts.length - 1] ?? slug;
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

  const aiTitle = await readAiTitle(path);
  const name = aiTitle ?? projectLabel(projectDir);
  const st: SessionStatus = ago <= ONGOING_MS ? "working" : "idle";

  return { n: name, st, ago };
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

  out.sort((a, b) => a.ago - b.ago);
  return out;
}
