// Use to verify wiring before flashing the main firmware.
// ramps fans 100% → 0% and repeats.

// Timer1 TOP value for 25kHz phase-correct PWM
// f_PWM = f_clk / (2 * prescaler * TOP)
// 25000 = 16000000 / (2 * 1 * 320)  →  TOP = 320
const uint16_t PWM_TOP = 320;

void setup() {
  // Set D9 as output (Timer1 OC1A)
  pinMode(9, OUTPUT);

  // Configure Timer1 for phase-correct PWM, TOP = ICR1
  //   WGM mode 10: phase-correct PWM with ICR1 as TOP
  //   COM1A1 set: non-inverting output on OC1A (D9)
  //   CS10 set: prescaler = 1 (no prescaling)
  TCCR1A = (1 << COM1A1) | (1 << WGM11);
  TCCR1B = (1 << WGM13) | (1 << CS10);
  ICR1 = PWM_TOP;

  OCR1A = PWM_TOP;
}

void setPWMPercent(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  OCR1A = (uint16_t)((long)PWM_TOP * percent / 100);
}

void loop() {
  for (int pct = 100; pct >= 0; pct--) {
    setPWMPercent(pct);
    delay(100);
  }

  setPWMPercent(100);
}
