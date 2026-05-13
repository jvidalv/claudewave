// Dev helper: run the scanner once and print the line-protocol frame
// that would be sent to the firmware.

import { scanSessions } from "./scanner.ts";

const sessions = await scanSessions();
const lines: string[] = ["BEGIN"];
for (const s of sessions) {
  lines.push(`S\t${s.n.replace(/[\t\r\n]+/g, " ")}\t${s.st}\t${s.ago}`);
}
lines.push("END");
console.log(lines.join("\n"));
console.error(`\n${sessions.length} session(s)`);
