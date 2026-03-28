/*
DummyFanX serial client example.

Connects to the Arduino, sends a duty cycle, and prints
incoming MB/RPM reports. Requires serialport

cargo run -- COM3 50
*/

use std::io::{BufRead, BufReader, Write};
use std::time::Duration;
use std::{env, thread};

fn xor_checksum(payload: &str) -> u8 {
    payload.bytes().fold(0u8, |acc, b| acc ^ b)
}

fn send(port: &mut Box<dyn serialport::SerialPort>, payload: &str) {
    let cs = xor_checksum(payload);
    let line = format!("{}*{:02X}\n", payload, cs);
    let _ = port.write_all(line.as_bytes());
}

fn validate(line: &str) -> Option<&str> {
    let pos = line.rfind('*')?;
    let payload = &line[..pos];
    let cs_hex = &line[pos + 1..];
    let received = u8::from_str_radix(cs_hex, 16).ok()?;
    if received == xor_checksum(payload) {
        Some(payload)
    } else {
        None
    }
}

fn main() {
    let args: Vec<String> = env::args().collect();
    let port_name = args.get(1).map(|s| s.as_str()).unwrap_or("COM3");
    let duty: u8 = args.get(2).and_then(|s| s.parse().ok()).unwrap_or(50);

    let port = serialport::new(port_name, 9600)
        .timeout(Duration::from_millis(100))
        .open()
        .expect("failed to open port");

    let mut writer = port.try_clone().expect("failed to clone port");
    let reader = BufReader::new(port);

    println!("Connected to {port_name}, setting duty to {duty}%");
    println!("Waiting for Arduino startup (3s)...");
    thread::sleep(Duration::from_secs(3));

    // read thread
    let handle = thread::spawn(move || {
        for line in reader.lines() {
            let line = match line {
                Ok(l) => l,
                Err(_) => continue,
            };
            if let Some(payload) = validate(line.trim()) {
                println!("{payload}");
            }
        }
    });

    // write loop
    loop {
        send(&mut writer, &duty.to_string());
        thread::sleep(Duration::from_millis(500));
    }
}
