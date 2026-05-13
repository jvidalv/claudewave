// ClaudeWave host: scans ~/.claude/projects every POLL_INTERVAL_MS and
// streams a snapshot to the firmware as a line-based frame:
//
//   BEGIN
//   S\tname\tstatus\tago
//   ...
//   END
//
// Survives device resets by reopening the port automatically.

import { SerialPort } from "serialport";
import { scanSessions } from "./scanner.ts";

const POLL_INTERVAL_MS = 10_000;
const RECONNECT_DELAY_MS = 1500;
const BAUD = 115200;

async function findPort(): Promise<string> {
  const explicit = process.env.CLAUDEWAVE_PORT;
  if (explicit) return explicit;

  const ports = await SerialPort.list();
  const match = ports.find(
    (p) => p.vendorId?.toLowerCase() === "303a" && p.productId?.toLowerCase() === "1001",
  );
  if (match) return match.path;

  const usbmodem = ports.find((p) => p.path.includes("usbmodem"));
  if (usbmodem) return usbmodem.path;

  throw new Error("No ClaudeWave device found.");
}

async function openPort(path: string): Promise<SerialPort> {
  const port = new SerialPort({ path, baudRate: BAUD, autoOpen: false });
  await new Promise<void>((resolve, reject) => {
    port.open((err) => (err ? reject(err) : resolve()));
  });
  return port;
}

function write(port: SerialPort, line: string): Promise<void> {
  return new Promise((resolve, reject) => {
    port.write(line, (err) => (err ? reject(err) : resolve()));
  });
}

function drain(port: SerialPort): Promise<void> {
  return new Promise((resolve) => port.drain(() => resolve()));
}

async function pushOnce(port: SerialPort): Promise<void> {
  const sessions = await scanSessions();
  // Drop tabs/newlines from names so the line protocol stays unambiguous.
  const lines: string[] = ["BEGIN"];
  for (const s of sessions) {
    const safeName = s.n.replace(/[\t\r\n]+/g, " ");
    lines.push(`S\t${safeName}\t${s.st}\t${s.ago}`);
  }
  lines.push("END");
  const frame = lines.join("\n") + "\n";
  await write(port, frame);
  await drain(port);
  console.error(`[host] sent ${sessions.length} sessions (${frame.length}B)`);
}

async function runOnce(): Promise<void> {
  const path = await findPort();
  console.error(`[host] opening ${path}`);
  const port = await openPort(path);

  let dead = false;
  const die = (reason: string, err?: Error): void => {
    if (dead) return;
    dead = true;
    console.error(`[host] disconnected (${reason})${err ? `: ${err.message}` : ""}`);
  };

  port.on("close", () => die("close"));
  port.on("error", (err) => die("error", err));

  while (!dead) {
    try {
      await pushOnce(port);
    } catch (err) {
      die("write", err as Error);
      break;
    }
    await new Promise<void>((r) => setTimeout(r, POLL_INTERVAL_MS));
  }

  try {
    port.close();
  } catch {
    /* already closed */
  }
}

async function main(): Promise<void> {
  while (true) {
    try {
      await runOnce();
    } catch (err) {
      console.error("[host] connect failed:", (err as Error).message);
    }
    await new Promise<void>((r) => setTimeout(r, RECONNECT_DELAY_MS));
  }
}

main().catch((err) => {
  console.error("[host] fatal:", err);
  process.exit(1);
});
