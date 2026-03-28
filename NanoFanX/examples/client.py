"""
DummyFanX serial client example.

Connects to the Arduino, sends a duty cycle, and prints
incoming MB/RPM reports. Requires pyserial: pip install pyserial
"""

import serial
import time
import sys


def xor_checksum(payload: str) -> str:
    cs = 0
    for b in payload.encode():
        cs ^= b
    return f"{cs:02X}"


def send(port: serial.Serial, payload: str):
    line = f"{payload}*{xor_checksum(payload)}\n"
    port.write(line.encode())


def parse_line(raw: str) -> tuple[str, bool]:
    """Validate checksum, return (payload, valid)."""
    raw = raw.strip()
    if "*" not in raw:
        return raw, False
    payload, cs_hex = raw.rsplit("*", 1)
    try:
        expected = xor_checksum(payload)
        return payload, cs_hex.upper() == expected
    except ValueError:
        return raw, False


def main():
    port_name = sys.argv[1] if len(sys.argv) > 1 else "COM3"
    duty = int(sys.argv[2]) if len(sys.argv) > 2 else 50

    with serial.Serial(port_name, 9600, timeout=1) as port:
        print(f"Connected to {port_name}, setting duty to {duty}%")
        print("Waiting for Arduino startup (3s)...")
        time.sleep(3)

        while True:
            send(port, str(duty))

            while port.in_waiting:
                line = port.readline().decode(errors="replace")
                payload, valid = parse_line(line)
                if not valid:
                    continue
                print(payload)

            time.sleep(0.5)


if __name__ == "__main__":
    main()
