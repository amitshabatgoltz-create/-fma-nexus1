/*
 * FMA Tech — Nexus-1 Smart Glove firmware
 * Board: Arduino Nano (ATmega328P), 5V supply, common GND for all parts.
 *
 * Buttons (to GND, internal pull-ups):
 *   RED   -> D2  : heat     OFF -> WARM -> HOT -> OFF
 *   GREEN -> D4  : massage  OFF -> LOW -> MEDIUM -> HIGH -> PULSE -> OFF
 *   BLUE  -> D7  : lights   OFF -> STEADY -> BLINK -> BREATHE -> OFF
 *   Hold any button for 2 seconds -> everything OFF.
 *
 * Outputs (PWM, each through a logic-level N-MOSFET — never drive the
 * loads straight from the pins, a Nano pin gives ~20mA):
 *   D9  -> yellow COB LED strips, all 5 fingers
 *   D5  -> vibration / massage motors (flyback diode across each motor)
 *   D6  -> 5V heating element at the wrist
 *
 * Optional: 10k NTC thermistor on A0 (divider with a 10k resistor to 5V)
 * placed next to the heater. Set USE_THERMISTOR to 1 to hold a steady
 * temperature; without it the heater runs at a fixed, capped duty cycle.
 */

// ---------- Pins ----------
const uint8_t PIN_BTN_RED   = 2;
const uint8_t PIN_BTN_GREEN = 4;
const uint8_t PIN_BTN_BLUE  = 7;

const uint8_t PIN_LEDS   = 9;
const uint8_t PIN_MOTORS = 5;
const uint8_t PIN_HEATER = 6;
const uint8_t PIN_THERM  = A0;

// ---------- Settings ----------
#define USE_THERMISTOR 0

const unsigned long DEBOUNCE_MS   = 30;
const unsigned long LONG_PRESS_MS = 2000;

// Massage strength (PWM 0-255). Coin vibration motors are usually rated
// for 3V, so the top level stays around 160 (~3V average from a 5V supply).
const uint8_t MOTOR_LOW    = 80;
const uint8_t MOTOR_MEDIUM = 120;
const uint8_t MOTOR_HIGH   = 160;

// Heat. Skin-safe: target never above 42°C, and the heater switches itself
// off after HEAT_TIMEOUT_MS so it can't be left on by accident.
const uint8_t       HEAT_DUTY_WARM  = 120;   // open-loop duty (no thermistor)
const uint8_t       HEAT_DUTY_HOT   = 180;   // hard cap on duty in all modes
const float         HEAT_TARGET_WARM = 38.0; // °C (thermistor mode)
const float         HEAT_TARGET_HOT  = 41.0;
const float         HEAT_MAX_C       = 43.0; // above this: heater off, always
const unsigned long HEAT_TIMEOUT_MS  = 20UL * 60UL * 1000UL;

// LED brightness and effect timing
const uint8_t       LED_MAX        = 255;
const unsigned long BLINK_HALF_MS  = 350;
const unsigned long BREATHE_PERIOD = 3000;

// ---------- Modes ----------
enum HeatMode    { HEAT_OFF, HEAT_WARM, HEAT_HOT, HEAT_COUNT };
enum MassageMode { MASSAGE_OFF, MASSAGE_LOW, MASSAGE_MEDIUM, MASSAGE_HIGH, MASSAGE_PULSE, MASSAGE_COUNT };
enum LightMode   { LIGHT_OFF, LIGHT_STEADY, LIGHT_BLINK, LIGHT_BREATHE, LIGHT_COUNT };

HeatMode    heatMode    = HEAT_OFF;
MassageMode massageMode = MASSAGE_OFF;
LightMode   lightMode   = LIGHT_OFF;

unsigned long heatStartedAt = 0;

// ---------- Buttons ----------
struct Button {
  uint8_t pin;
  bool stable;              // debounced level (true = pressed)
  bool lastRaw;
  unsigned long changedAt;
  unsigned long pressedAt;
  bool longFired;
};

Button btnRed   = { PIN_BTN_RED,   false, false, 0, 0, false };
Button btnGreen = { PIN_BTN_GREEN, false, false, 0, 0, false };
Button btnBlue  = { PIN_BTN_BLUE,  false, false, 0, 0, false };

// Returns 1 on a short press (fired on release), 2 on a long press
// (fired once while held), 0 otherwise.
uint8_t readButton(Button &b, unsigned long now) {
  bool raw = digitalRead(b.pin) == LOW;
  if (raw != b.lastRaw) {
    b.lastRaw = raw;
    b.changedAt = now;
  }
  if (now - b.changedAt >= DEBOUNCE_MS && raw != b.stable) {
    b.stable = raw;
    if (b.stable) {
      b.pressedAt = now;
      b.longFired = false;
    } else if (!b.longFired) {
      return 1;
    }
  }
  if (b.stable && !b.longFired && now - b.pressedAt >= LONG_PRESS_MS) {
    b.longFired = true;
    return 2;
  }
  return 0;
}

void allOff() {
  heatMode = HEAT_OFF;
  massageMode = MASSAGE_OFF;
  lightMode = LIGHT_OFF;
  analogWrite(PIN_HEATER, 0);
  analogWrite(PIN_MOTORS, 0);
  analogWrite(PIN_LEDS, 0);
}

// ---------- Lights ----------
void updateLights(unsigned long now) {
  uint8_t level = 0;
  switch (lightMode) {
    case LIGHT_OFF:
      level = 0;
      break;
    case LIGHT_STEADY:
      level = LED_MAX;
      break;
    case LIGHT_BLINK:
      level = ((now / BLINK_HALF_MS) % 2 == 0) ? LED_MAX : 0;
      break;
    case LIGHT_BREATHE: {
      // Smooth 0..1..0 cosine wave, squared so it looks even to the eye.
      float phase = (float)(now % BREATHE_PERIOD) / BREATHE_PERIOD;
      float wave = 0.5f - 0.5f * cos(2.0f * PI * phase);
      level = (uint8_t)(wave * wave * LED_MAX);
      break;
    }
    default:
      break;
  }
  analogWrite(PIN_LEDS, level);
}

// ---------- Massage ----------
void updateMassage(unsigned long now) {
  uint8_t level = 0;
  switch (massageMode) {
    case MASSAGE_OFF:    level = 0; break;
    case MASSAGE_LOW:    level = MOTOR_LOW; break;
    case MASSAGE_MEDIUM: level = MOTOR_MEDIUM; break;
    case MASSAGE_HIGH:   level = MOTOR_HIGH; break;
    case MASSAGE_PULSE: {
      // Repeating pattern: short-short-long pulses with changing strength.
      static const uint16_t stepMs[]   = { 200, 150, 200, 150, 600, 500 };
      static const uint8_t  stepLvl[]  = { MOTOR_HIGH, 0, MOTOR_MEDIUM, 0, MOTOR_HIGH, 0 };
      const uint8_t steps = sizeof(stepMs) / sizeof(stepMs[0]);
      unsigned long total = 0;
      for (uint8_t i = 0; i < steps; i++) total += stepMs[i];
      unsigned long t = now % total;
      for (uint8_t i = 0; i < steps; i++) {
        if (t < stepMs[i]) { level = stepLvl[i]; break; }
        t -= stepMs[i];
      }
      break;
    }
    default:
      break;
  }
  analogWrite(PIN_MOTORS, level);
}

// ---------- Heat ----------
#if USE_THERMISTOR
float readTemperatureC() {
  const float R_FIXED = 10000.0, R_NOMINAL = 10000.0, T_NOMINAL = 25.0, BETA = 3950.0;
  int adc = analogRead(PIN_THERM);
  if (adc <= 0 || adc >= 1023) return NAN;   // open / shorted sensor
  float r = R_FIXED * adc / (1023.0 - adc);  // NTC on the GND side
  float k = 1.0 / (log(r / R_NOMINAL) / BETA + 1.0 / (T_NOMINAL + 273.15));
  return k - 273.15;
}
#endif

void updateHeat(unsigned long now) {
  if (heatMode == HEAT_OFF) {
    analogWrite(PIN_HEATER, 0);
    return;
  }
  if (now - heatStartedAt >= HEAT_TIMEOUT_MS) {
    heatMode = HEAT_OFF;
    analogWrite(PIN_HEATER, 0);
    return;
  }

#if USE_THERMISTOR
  static unsigned long lastCheck = 0;
  static uint8_t duty = 0;
  if (now - lastCheck >= 500) {
    lastCheck = now;
    float t = readTemperatureC();
    float target = (heatMode == HEAT_HOT) ? HEAT_TARGET_HOT : HEAT_TARGET_WARM;
    if (isnan(t) || t >= HEAT_MAX_C) {
      duty = 0;                                  // sensor fault or too hot
    } else {
      // Proportional control with 0.5°C hysteresis band near the target.
      float err = target - t;
      if (err <= -0.5) duty = 0;
      else duty = (uint8_t)constrain(err * 60.0 + 40.0, 0, HEAT_DUTY_HOT);
    }
  }
  analogWrite(PIN_HEATER, duty);
#else
  analogWrite(PIN_HEATER, heatMode == HEAT_HOT ? HEAT_DUTY_HOT : HEAT_DUTY_WARM);
#endif
}

// ---------- Main ----------
void setup() {
  pinMode(PIN_BTN_RED, INPUT_PULLUP);
  pinMode(PIN_BTN_GREEN, INPUT_PULLUP);
  pinMode(PIN_BTN_BLUE, INPUT_PULLUP);
  pinMode(PIN_LEDS, OUTPUT);
  pinMode(PIN_MOTORS, OUTPUT);
  pinMode(PIN_HEATER, OUTPUT);
  allOff();
  Serial.begin(9600);
  Serial.println(F("FMA Tech Nexus-1 ready"));
}

void loop() {
  unsigned long now = millis();

  uint8_t red   = readButton(btnRed, now);
  uint8_t green = readButton(btnGreen, now);
  uint8_t blue  = readButton(btnBlue, now);

  if (red == 2 || green == 2 || blue == 2) {
    allOff();
    Serial.println(F("ALL OFF"));
  } else {
    if (red == 1) {
      heatMode = (HeatMode)((heatMode + 1) % HEAT_COUNT);
      heatStartedAt = now;
      Serial.print(F("Heat mode: "));    Serial.println(heatMode);
    }
    if (green == 1) {
      massageMode = (MassageMode)((massageMode + 1) % MASSAGE_COUNT);
      Serial.print(F("Massage mode: ")); Serial.println(massageMode);
    }
    if (blue == 1) {
      lightMode = (LightMode)((lightMode + 1) % LIGHT_COUNT);
      Serial.print(F("Light mode: "));   Serial.println(lightMode);
    }
  }

  updateLights(now);
  updateMassage(now);
  updateHeat(now);
}
