// Dev helper: run the scanner once and print what would be sent to the
// firmware. Not part of the main loop.

import { scanSessions } from "./scanner.ts";

const snapshots = await scanSessions();
const payload = { v: 1, s: snapshots };
console.log(JSON.stringify(payload, null, 2));
console.log(`\n${snapshots.length} session(s)`);
