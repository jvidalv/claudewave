# ClaudeWave

Firmware for the **Waveshare ESP32-S3-Touch-AMOLED-2.41-B** that will
eventually render the state of every running Claude Code session — think
`claude agents` on a 600×450 AMOLED watch face.

This first commit only does the simplest thing: boot, init the panel, draw
the digit **1**. Networking will come next over BLE, driven by a TypeScript
host on the laptop.

## Hardware

- ESP32-S3R8 — 16 MB flash, 8 MB octal PSRAM
- 2.41″ AMOLED, **600 × 450**, CO5300/RM690B0-class controller, QSPI
- FT-series capacitive touch, IMU (QMI8658), RTC (PCF85063) — all on I²C
- Native USB on GPIO19/20 (no UART bridge), enumerates as
  `VID:PID 303A:1001`

GPIO map (taken from the official `arduino-esp32` board variant
`waveshare_esp32_s3_touch_amoled_241`):

| Signal     | GPIO |
| ---------- | ---- |
| QSPI CS    | 9    |
| QSPI SCK   | 10   |
| QSPI D0–D3 | 11–14 |
| AMOLED RST | 21   |
| I²C SDA    | 47   |
| I²C SCL    | 48   |
| BAT ADC    | 17   |

## Build & flash

```sh
brew install platformio
pio run -t upload
pio device monitor
```

The board appears as `/dev/cu.usbmodemXXXX` once connected over USB-C. If
nothing shows up, the cable is almost certainly power-only — swap it.

## Host (TypeScript)

A small Node + TypeScript scanner under `host/` watches
`~/.claude/projects/**/*.jsonl` and pushes a sessions snapshot to the
firmware over USB-CDC every second.

```sh
cd host
pnpm install
pnpm dev   # talks to /dev/cu.usbmodem* automatically; override via CLAUDEWAVE_PORT
```

Sessions older than 1 h are filtered out client-side; the firmware
collapses anything beyond 6 visible rows into "+N inactive".

## Roadmap

1. ✅ Draw "1" on the panel
2. ✅ Animated splash (lifted from Clawdmeter)
3. ✅ Sessions screen with status icons
4. ✅ Real data over USB-CDC, fed by a TypeScript host
5. ⬜ Replace USB with BLE GATT so the device runs off-cable
6. ⬜ Touch interactions (tap a session to expand?)

## License

MIT — see [LICENSE](LICENSE). Toy project, do whatever.
