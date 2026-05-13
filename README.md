# ClaudeWave

A tiny status display for [Claude Code](https://claude.com/claude-code).

ClaudeWave runs on a Waveshare ESP32-S3 AMOLED board sitting on your
desk. A small TypeScript host watches `~/.claude/projects/` and pushes
the state of every session to the device over USB-CDC: which agents are
working, which are waiting on you, which are done, which errored.

The end goal is "glance at the display, know which of my Claude agents
needs me." Today it's USB-tethered; BLE is the next milestone.

## What it shows

There are two screens. The firmware switches between them based on
whether anything's happening:

**Sessions screen** — up to six rows, sorted most-recently-active first.
Each row is an icon plus the session name. Anything beyond six collapses
into a `+N inactive` counter at the bottom.

| Icon | Status | Meaning |
| --- | --- | --- |
| 🔵 blue comet spinner | `working` | agent is in flight (tool turn / generating) |
| 🟠 pulsing amber `!` | `waiting` | last assistant turn ended — your move |
| ⏸ gray bars | `idle` | session open, nothing pending |
| ✅ green check | `done` | session closed cleanly |
| ❌ red X | `error` | last event failed |

**Splash screen** — animated 20×20 pixel-art creatures (lifted from
[Clawdmeter](https://github.com/HermannBjorgvin/Clawdmeter)) with a
"claudewave" wordmark on top. Shown when there's been no session
activity for 15 minutes; the wordmark colour tracks the current
creature's body palette.

## Hardware

Built for the **Waveshare ESP32-S3-Touch-AMOLED-2.41-B** as-is — no
soldering. Should port to the 1.8/2.06 with a pin-map change.

- ESP32-S3R8, 16 MB flash, 8 MB octal PSRAM
- 2.41″ AMOLED, 450 × 600 portrait, CO5300/RM690B0-class QSPI controller
- Capacitive touch (unused for now), IMU, RTC — all on I²C
- Native USB-CDC, no UART bridge — enumerates as `VID:PID 303A:1001`

GPIO map (from the official `espressif/arduino-esp32` variant
[`waveshare_esp32_s3_touch_amoled_241`](https://github.com/espressif/arduino-esp32/blob/master/variants/waveshare_esp32_s3_touch_amoled_241/pins_arduino.h)):

| Signal     | GPIO  |
| ---------- | ----- |
| QSPI CS    | 9     |
| QSPI SCK   | 10    |
| QSPI D0–D3 | 11–14 |
| AMOLED RST | 21    |
| I²C SDA    | 47    |
| I²C SCL    | 48    |
| BAT ADC    | 17    |

Note: the CO5300 has no hardware rotation, so the firmware uses the
panel's native portrait orientation. Hold the device with the USB-C on
the short bottom edge.

## Layout

```
.
├── platformio.ini      # firmware build config (PlatformIO + Arduino 3.x)
├── src/
│   ├── main.cpp        # boot, screen switch
│   ├── splash.{h,cpp}  # animated creature renderer
│   ├── splash_animations.h    # 13 animations × 16 frames each
│   ├── sessions.{h,cpp}       # session list screen
│   └── transport.{h,cpp}      # USB-CDC JSON ingest
└── host/               # TypeScript host (Node 22, pnpm)
    └── src/
        ├── index.ts    # serial connect, push loop, auto-reconnect
        └── scanner.ts  # ~/.claude/projects/ → session snapshots
```

## Build & flash

```sh
brew install platformio
pio run -t upload
pio device monitor   # 115200 baud
```

The board appears as `/dev/cu.usbmodemXXXX` over USB-C. If nothing shows
up, the cable is almost certainly power-only — swap it.

## Run the host

```sh
cd host
pnpm install
pnpm dev             # talks to /dev/cu.usbmodem* automatically
```

Override the port with `CLAUDEWAVE_PORT=/dev/cu.usbmodem1101 pnpm dev`.

The host auto-reconnects if you reset the board (`ENXIO` is caught,
backoff 1.5 s, reopen).

### Status detection

The host walks each session's `.jsonl` tail to derive `waiting`, `done`,
`error`, and `idle`. On top of that, any session file touched within the
last **30 seconds** is force-marked `working` regardless of event type
— the JSONL gets a new line on every assistant → tool → user round-trip,
so fresh mtime ≡ actively doing something.

Sessions older than 1 h are dropped client-side. Polling cadence: 10 s.

## Wire format

One newline-terminated JSON line per snapshot:

```json
{"v": 1, "s": [
  {"n": "port-clawdmeter-splash-animations", "st": "working", "ago": 1264},
  {"n": "image-editor-chat-binding",          "st": "idle",    "ago": 740020}
]}
```

`n` = display name (the model's own `aiTitle`, falling back to the
project's basename). `st` ∈ `working | waiting | idle | done | error`.
`ago` = ms since this session's last activity, computed host-side so the
device doesn't need a clock.

## Credits

- Splash animations and original render kernel from
  [HermannBjorgvin/Clawdmeter](https://github.com/HermannBjorgvin/Clawdmeter)
  (MIT). The art is sourced from [claudepix.vercel.app](https://claudepix.vercel.app).
- Display driver via
  [moononournation/Arduino_GFX](https://github.com/moononournation/Arduino_GFX).
- Toolchain: [pioarduino](https://github.com/pioarduino/platform-espressif32).

## License

MIT — see [LICENSE](LICENSE).
