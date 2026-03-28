# ArduinoFanX

Open-source Arduino fan controller firmware. Drives 4-pin PWM fans via a fan hub, with optional motherboard PWM mirroring, RPM sensing, and a physical speed button.

I made a paid companion app, DummyFanX, that adds a GUI with fan curves, temperature monitoring, and profiles, but if you don't want it, you won't need it. The firmware works standalone, and you're free to build your own software on top of it. The serial protocol is documented, and example clients are included.

## Features

- **PWM Fan Control** — Set fan speed 0–100% via serial command, button preset, or motherboard PWM signal
- **Motherboard PWM Mirroring** — Automatically follow your BIOS fan curve (optional, D2 input)
- **RPM Feedback** — Read fan speed via tachometer input (optional, D3 input)
- **Physical Speed Button** — Cycle through 5 configurable speed presets without a PC (D4 input)
- **Fallback Modes** — Gracefully degrade: PC → Motherboard → Default preset if signal is lost
- **25 kHz PWM** — Compatible with most fan hubs

## Boards

| Board | Status | Directory |
|-------|--------|-----------|
| Arduino Nano (ATmega328P) | Ready | [NanoFanX/](NanoFanX/) |

Each board has its own firmware, manual, and example clients. See the board directory for everything you need.

## Wiring (Nano)

See [NanoFanX/diagram/diagram.png](NanoFanX/diagram/diagram.png) for the full schematic. Quick pinout:

| Pin | Purpose |
|-----|---------|
| D9  | PWM output to fan hub (OC1A) |
| D2  | Motherboard PWM input (optional, INT0) |
| D3  | Fan tachometer input (optional, INT1) |
| D4  | Speed preset button (optional, pulled to GND) |
| GND | Share ground with fan hub and any inputs |

## Quick Start (Nano)

1. Wire D9 to your fan hub's PWM pin, share GND
2. Flash: `arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328old -u -p COM3 NanoFanX/firmware/main/`
3. Done — fans run at 50%. Wire a button to D4 to cycle speeds.

Full details in [NanoFanX/manual.md](NanoFanX/manual.md).

## Serial Protocol

Send commands via 9600 baud serial (USB or UART):

- **`50*<checksum>`** — Set fan to 50% (any 0–100)
- **`H*<checksum>`** — Heartbeat (keeps PC mode active)

Checksum is XOR of all bytes before the `*`. The device echoes back `MB:<duty>` and `RPM:<speed>` every 1 second.

See [NanoFanX/examples/](NanoFanX/examples/) for Python and Rust client code.

## Boards

Want to add support for another board (UNO, ESP32, STM32, etc.)? Create a new directory:

```
YourBoardFanX/
├── firmware/
│   └── main/
│       └── main.ino
├── examples/
│   └── client.py
└── manual.md
```

Keep the same serial protocol if possible so existing client code works across boards. If your board needs a different protocol (e.g. WiFi instead of serial), document it in your manual.

## Project Status

**Stable.** Arduino Nano firmware is production-ready. Basic testing (test_ramp.ino) included; run it before flashing main firmware to verify wiring.

## License

MIT - If your fans spin backwards, your cat starts barking, or your PC achieves sentience, it's not my fault.
