// constants

// Timer1 TOP for 25kHz phase-correct PWM
// f_PWM = f_clk / (2 * prescaler * TOP) = 16000000 / (2 * 1 * 320) = 25000
const uint16_t PWM_TOP = 320;

const uint8_t PIN_PWM_OUT = 9;  // D9 — OC1A, Timer1 output
const uint8_t PIN_MB_PWM = 2;   // D2 — INT0, MB PWM input
const uint8_t PIN_FAN_TACH = 3; // D3 — INT1, fan tach input (optional)
const uint8_t PIN_BUTTON = 4;   // D4 — speed preset button (to GND)

const unsigned long HEARTBEAT_TIMEOUT_MS = 5000; // PC considered gone after 5s
const unsigned long MB_TIMEOUT_MS = 1000;        // MB considered gone after 1s
const unsigned long MB_GRACE_MS = 200;           // hold last MB value during brief signal dropouts
const unsigned long REPORT_INTERVAL_MS = 1000;   // serial report every 1s
const unsigned long STARTUP_FULL_MS = 3000;      // 100% at startup for 3s

const int MB_AVG_SAMPLES = 100;  // periods to average for MB duty reading

// Button speed presets — edit this array to customize speeds
const int BUTTON_PRESETS[] = {0, 25, 50, 75, 100};
const int NUM_PRESETS = sizeof(BUTTON_PRESETS) / sizeof(BUTTON_PRESETS[0]);
const unsigned long DEBOUNCE_MS = 50;

// state

enum ControlMode { MODE_PC, MODE_MB, MODE_DEFAULT };
ControlMode currentMode = MODE_DEFAULT;

unsigned long lastHeartbeatMs = 0;
int pcDuty = BUTTON_PRESETS[NUM_PRESETS / 2];  // last duty received from PC

// MB PWM measurement (ISR-shared, marked volatile)
volatile unsigned long isrRiseTime = 0;  // micros() at rising edge
volatile unsigned long isrHighTime = 0;  // high pulse duration (µs)
volatile unsigned long isrPeriod = 0;    // full period duration (µs)
volatile bool isrNewSample = false;
volatile unsigned long isrLastEdge = 0;  // last edge timestamp for timeout

unsigned long mbHighSum = 0;
unsigned long mbPeriodSum = 0;
int mbSampleCount = 0;
int mbDuty = 0;            // averaged result 0-100
int mbLastValidDuty = BUTTON_PRESETS[NUM_PRESETS / 2]; // last known good MB duty (for grace period)
bool mbWasPresent = false; // was MB present last loop?

volatile unsigned long tachPulseCount = 0;
unsigned long lastRpmCalcMs = 0;
unsigned int fanRpm = 0;
unsigned long lastReportMs = 0;

int presetIndex = NUM_PRESETS / 2; // start at middle preset
bool buttonLastReading = true;  // raw pin reading last loop (true = not pressed)
bool buttonPressed = false;     // debounced state: true = currently held
unsigned long buttonLastChange = 0;

char serialBuf[12];  // serial input buffer
uint8_t serialBufPos = 0;

// pwm output

void setupPWM() {
  pinMode(PIN_PWM_OUT, OUTPUT);

  // Timer1: phase-correct PWM, TOP = ICR1, no prescaling (datasheet table 16-4, mode 10)
  // COM1A1 for non-inverting output on OC1A (D9)
  TCCR1A = (1 << COM1A1) | (1 << WGM11);
  TCCR1B = (1 << WGM13) | (1 << CS10);
  ICR1 = PWM_TOP;
}

void setPWMPercent(int percent) {
  if (percent < 0) { 
    percent = 0;
  } else if (percent > 100) {
    percent = 100;
  }
  OCR1A = (uint16_t)((long)PWM_TOP * percent / 100);
}

// mb pwm input (ISR)

void mbPwmISR() {
  unsigned long now = micros();
  bool rising = (PIND & (1 << PD2)) != 0;

  if (rising) {
    // Rising edge: measure full period from last rise
    if (isrRiseTime > 0) {
      isrPeriod = now - isrRiseTime;
    }
    isrRiseTime = now;
  } else {
    // Falling edge: measure high time
    isrHighTime = now - isrRiseTime;
    isrNewSample = true;
  }

  isrLastEdge = now;
}

void setupMbInput() {
  pinMode(PIN_MB_PWM, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_MB_PWM), mbPwmISR, CHANGE);
}

// Collect ISR samples and average over MB_AVG_SAMPLES periods.
// Returns true if MB signal is present (or within grace period).
// Updates mbDuty. Holds last valid duty during brief signal dropouts.
bool updateMbDuty() {
  // Single atomic read of all ISR-shared state
  noInterrupts();
  unsigned long lastEdge = isrLastEdge;
  bool haveSample = isrNewSample;
  unsigned long highTime = isrHighTime;
  unsigned long period = isrPeriod;
  isrNewSample = false;
  interrupts();

  unsigned long silenceUs = (lastEdge > 0) ? (micros() - lastEdge) : 0;
  bool signalActive = (lastEdge > 0) && (silenceUs < (MB_GRACE_MS * 1000UL));
  bool signalLost = (lastEdge > 0) && (silenceUs >= (MB_TIMEOUT_MS * 1000UL));

  if (signalLost) {
    // Signal gone for good — reset everything
    mbSampleCount = 0;
    mbHighSum = 0;
    mbPeriodSum = 0;
    mbWasPresent = false;
    return false;
  }

  if (haveSample && period > 0) {
    mbHighSum += highTime;
    mbPeriodSum += period;
    mbSampleCount++;

    if (mbSampleCount >= MB_AVG_SAMPLES) {
      // Compute averaged duty
      mbDuty = (int)((mbHighSum * 100UL) / mbPeriodSum);
      if (mbDuty < 0) { // just in case math stops working
        mbDuty = 0;
      } else if (mbDuty > 100) {
        mbDuty = 100;
      }

      mbLastValidDuty = mbDuty;  // save for grace period
      mbWasPresent = true;

      // Reset for next batch
      mbHighSum = 0;
      mbPeriodSum = 0;
      mbSampleCount = 0;
    }
  }

  // During grace period (signal dropped briefly), hold last valid duty
  if (!signalActive && mbWasPresent) {
    mbDuty = mbLastValidDuty;
    return true;  // still "present" during grace
  }

  return signalActive || mbWasPresent;
}

void tachISR() {
  tachPulseCount++;
}

void setupTach() {
  pinMode(PIN_FAN_TACH, INPUT_PULLUP);  // tach is open-drain, needs pull-up
  attachInterrupt(digitalPinToInterrupt(PIN_FAN_TACH), tachISR, FALLING);
}

// Call every report interval.
void updateRpm(unsigned long now) {
  unsigned long elapsed = now - lastRpmCalcMs;
  if (elapsed == 0) {
    return;
  }

  noInterrupts();
  unsigned long pulses = tachPulseCount;
  tachPulseCount = 0;
  interrupts();

  fanRpm = (unsigned int)((pulses * 30000UL) / elapsed);
  lastRpmCalcMs = now;
}

void setupButton() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
}

// Check button with debounce. Returns true once per press (on the falling edge).
bool updateButton(unsigned long now) {
  bool reading = digitalRead(PIN_BUTTON);

  if (reading != buttonLastReading) {
    buttonLastChange = now;
  }
  buttonLastReading = reading;

  if ((now - buttonLastChange) < DEBOUNCE_MS) {
    return false;  // still bouncing
  }

  //  detect transition to pressed
  bool nowPressed = !reading;
  if (nowPressed && !buttonPressed) {
    buttonPressed = true;
    return true;
  }
  if (!nowPressed) {
    buttonPressed = false;
  }
  return false;
}

// serial

uint8_t xorChecksum(const char *s) {
  uint8_t cs = 0;
  while (*s) {
    cs ^= (uint8_t)*s++;
  }
  return cs;
}

void sendChecksummed(const char *msg) {
  uint8_t cs = xorChecksum(msg);
  Serial.print(msg);
  Serial.print('*');
  if (cs < 0x10) {
    Serial.print('0');
  }
  Serial.println(cs, HEX);
}

// Process a validated command (checksum already stripped from serialBuf)
void processCommand(uint8_t len) {
  if (serialBuf[0] == 'H' && len == 1) {
    lastHeartbeatMs = millis();
    return;
  }

  int val = atoi(serialBuf);
  if (val < 0 || val > 100) {
    return;
  }

  pcDuty = val;
  lastHeartbeatMs = millis();
}

void processSerialInput() {
  while (Serial.available()) {
    char c = Serial.read();

    if (serialBufPos >= sizeof(serialBuf) - 1) {
      serialBufPos = 0; 
      continue;
    }

    if (c != '\n' && c != '\r') {
      serialBuf[serialBufPos++] = c;
      continue;
    }

    if (serialBufPos == 0) {
      continue;
    }
    serialBuf[serialBufPos] = '\0';
    serialBufPos = 0;

    char *star = strchr(serialBuf, '*');
    if (star == NULL) {
      continue;
    }

    *star = '\0';
    uint8_t expected = xorChecksum(serialBuf);
    uint8_t received = (uint8_t)strtoul(star + 1, NULL, 16);
    if (expected != received) {
      continue;
    }

    processCommand((uint8_t)(star - serialBuf));
  }
}

void sendReport(bool mbPresent) {
  if (mbPresent) {
    char buf[8];  // "MB:" + up to 3 digits + null
    strcpy(buf, "MB:");
    itoa(mbDuty, buf + 3, 10);
    sendChecksummed(buf);
  } else {
    sendChecksummed("MB:DISCONNECTED");
  }

  char buf[10];  // "RPM:" + up to 5 digits + null
  strcpy(buf, "RPM:");
  utoa(fanRpm, buf + 4, 10);
  sendChecksummed(buf);
}

// entry

void setup() {
  setupPWM();

  // blast the fans for 3s
  setPWMPercent(100);
  delay(STARTUP_FULL_MS);

  // Drop to default preset
  setPWMPercent(BUTTON_PRESETS[NUM_PRESETS / 2]);

  setupMbInput();
  setupTach();
  setupButton();
  lastRpmCalcMs = millis();
  Serial.begin(9600);
}

void loop() {
  unsigned long now = millis();

  processSerialInput();
  bool mbPresent = updateMbDuty();
  bool pcAlive = (lastHeartbeatMs > 0) && (now - lastHeartbeatMs < HEARTBEAT_TIMEOUT_MS);

  ControlMode newMode;
  if (pcAlive) {
    newMode = MODE_PC;
  } else if (mbPresent) {
    newMode = MODE_MB;
  } else {
    newMode = MODE_DEFAULT;
  }

  switch (newMode) {
    case MODE_PC:
      setPWMPercent(pcDuty);
      break;
    case MODE_MB:
      setPWMPercent(mbDuty);
      break;
    case MODE_DEFAULT:
      // On transition to default mode, apply current preset
      if (currentMode != MODE_DEFAULT) {
        setPWMPercent(BUTTON_PRESETS[presetIndex]);
      }
      // Button cycles through presets (only active in default mode)
      if (updateButton(now)) {
        presetIndex = (presetIndex + 1) % NUM_PRESETS;
        setPWMPercent(BUTTON_PRESETS[presetIndex]);
      }
      break;
  }

  currentMode = newMode;

  // Serial report every 1s
  if (now - lastReportMs >= REPORT_INTERVAL_MS) {
    updateRpm(now);
    lastReportMs = now;
    sendReport(mbPresent);
  }
}
