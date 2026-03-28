# NanoFanX Manual

Fan controller firmware for the Arduino Nano (ATmega328P, 16MHz). Drives 4-pin PWM fans at 25kHz, with optional motherboard PWM mirroring, RPM sensing, and a physical speed button.

## What You Need

- **Arduino Nano** (ATmega328P, 16MHz) — clones work fine
- **4-pin PWM fan hub** (or any 4-pin PWM fan) — powered via SATA from PSU
- **USB cable** — serial communication and power to the Nano
- **Push button** (optional) — momentary, normally open
- Hookup wire

## Wiring

```
Arduino Nano          Fan Hub / Peripherals
─────────────         ────────────────────────
D9  ────────────────► Fan Hub PIN 4 (PWM control)
D2  ◄──────────────── Motherboard Fan Header PIN 4 (PWM input, optional)
D3  ◄──────────────── Fan Tach PIN 3 (RPM sense, optional)
D4  ◄──────────────── Push button (other leg to GND, optional)
GND ────────────────► Fan Hub PIN 1 (GND, shared ground)
```

The fan hub gets 12V from a SATA connector off the PSU. The Arduino only provides the PWM control signal — it does NOT power the fans.

### Motherboard PWM Input (optional)

Connect your motherboard's CPU fan header to D2 and the Arduino mirrors whatever duty the motherboard sends. This lets you keep BIOS-level fan control while having the option to override via USB.

No motherboard header? No problem — falls back to button presets or USB control.

### Fan Tach (optional)

Wire a fan's tach wire (PIN 3) to D3 for RPM reporting over serial. Most fans are open-drain on tach, so the firmware enables the internal pull-up. Skip this wire if you don't care about RPM.

### Push Button (optional)

Wire a momentary button between D4 and GND. No resistor needed — uses the ATmega's internal pull-up.

Each press cycles through speed presets: **0% → 25% → 50% → 75% → 100%**

Customize by editing the array at the top of `main.ino`:
```c
const int BUTTON_PRESETS[] = {0, 25, 50, 75, 100};
```

The button only works when neither a PC app nor the motherboard PWM is active.

## Control Priority

1. **PC** — heartbeat received in the last 5s → USB serial commands set the duty
2. **Motherboard** — PWM signal present on D2 → mirror that duty
3. **Button** — cycle through presets (starts at 50%)

When the PC stops sending heartbeats, the Arduino falls through to MB mode or button mode automatically.

## Startup Behavior

On power-up, the Arduino runs all fans at 100% for 3 seconds to ensure spin-up, then drops to the default preset. Some fans won't start from a cold 25%.

## Flashing

Install [Arduino CLI](https://arduino.github.io/arduino-cli/installation/), then:

```bash
arduino-cli core install arduino:avr
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328old -u -p COM3 NanoFanX/firmware/main/
```

Replace `COM3` with your port. Run `arduino-cli board list` to find it.

### Test Sketch

`firmware/test_ramp/` contains a minimal sketch that ramps fans from 100% to 0% and repeats. Use it to verify your wiring before flashing the main firmware.

```bash
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328old -u -p COM3 NanoFanX/firmware/test_ramp/
```

## Serial Protocol

9600 baud, 8N1. All messages carry an NMEA-style XOR checksum: `payload*XX\n` where `XX` is the 2-digit uppercase hex XOR of every byte in the payload.

Example: `MB:75` → XOR of `M`,`B`,`:`,`7`,`5` = `0x37` → wire format `MB:75*37\n`

Messages with bad or missing checksums are silently dropped.

### PC → Arduino

| Message | Description |
|---------|-------------|
| `H*XX` | Heartbeat. Send at least every 5 seconds. |
| `{0-100}*XX` | Set fan duty (integer). Also counts as heartbeat. |

### Arduino → PC

Sent every ~1 second:

| Message | Description |
|---------|-------------|
| `MB:{0-100}*XX` | Motherboard PWM duty. |
| `MB:DISCONNECTED*XX` | No motherboard PWM signal. |
| `RPM:{value}*XX` | Fan RPM (2 pulses per revolution). |

### Timing

- Send heartbeats or duty commands every 500ms–2s. Faster is fine, slower risks the 5s timeout.
- Arduino reports every ~1 second. Don't assume exact timing.
- After opening the serial port, the Arduino resets and blasts fans at 100% for 3 seconds. Wait for the first `MB:` or `RPM:` message before assuming the link is live.

## Example Clients

Working client code in [examples/](examples/):

- **`client.py`** — Python (requires `pyserial`): `python client.py COM3 50`
- **`client.rs`** — Rust (requires `serialport` crate): `cargo run -- COM3 50`

Both connect to the Arduino, set a duty cycle, and print incoming MB/RPM reports.
