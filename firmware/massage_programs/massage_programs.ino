#include <avr/sleep.h>

const int MOTOR = 5;
const int LED = 13;  // נורית L: דולקת כשתוכנית פועלת

// ---- שלוש התוכניות: ירוק, כחול, אדום ----
const int BUTTON[3]            = {4, 7, 2};
const unsigned long MINUTES[3] = {5, 10, 15};
const int POWER[3]             = {130, 145, 160}; // חוזק הנגיעה (עד 160)
const int TAP_MS[3]            = {45, 60, 80};    // אורך נגיעה = כמה היא מורגשת

// הסיכוי של כל מצב בכל תוכנית:
//                 טיפות נשימה דופק גלגול מדרגות גשם לחיצה
const int WEIGHT[3][7] = {
                  {  31,   31,   31,    3,     3,    0,    0 },  // ירוק
                  {  20,   20,   20,   15,    15,    5,    5 },  // כחול
                  {  12,   12,   14,   16,    16,   15,   15 } };// אדום

enum Style { DROPS, BREATH, HEART, ROLL, STEPS, RAIN, PRESS };

const int KICK = 160;                    // דחיפה קצרה כדי שהמנוע יתחיל להסתובב מהר
const unsigned long GAP_BETWEEN = 500;   // שקט קצר בין מצב למצב
const unsigned long FADE_TIME = 3000;    // היחלשות בסוף
const unsigned long IDLE_SLEEP = 20000;  // שינה אחרי 20 שניות בלי כלום

// ---- מצב התוכנית ----
int program = -1;
bool fading = false, userStopped = false;
int pausedProgram = -1;
unsigned long pausedElapsed = 0;
unsigned long programStart = 0, fadeStart = 0, lastActivity = 0;

// ---- מצב העיסוי הנוכחי ----
int style = -1;
unsigned long styleStart = 0, styleEnd = 0;
int tempo = 100;            // שינוי קטן אקראי בקצב (85%-115%)
int step = 0;               // מספר הנגיעה בתוך המצב
unsigned long tapStart = 0, tapLen = 0, nextTap = 0;
int tapPower = 0;
bool tapRamp = false;       // נגיעה רכה שמתחזקת ונחלשת (מצב לחיצה)

bool lastState[3] = {HIGH, HIGH, HIGH};
unsigned long lastChange[3] = {0, 0, 0};

bool pressed(int i) {
  bool now = digitalRead(BUTTON[i]);
  if (now != lastState[i] && millis() - lastChange[i] > 50) {
    lastChange[i] = millis();
    lastState[i] = now;
    if (now == LOW) return true;
  }
  return false;
}

// בוחר מצב אקראי לפי הסיכויים של התוכנית, אף פעם לא אותו מצב פעמיים ברצף
void pickStyle(unsigned long now) {
  int total = 0;
  for (int s = 0; s < 7; s++) if (s != style) total += WEIGHT[program][s];
  int r = random(total);
  for (int s = 0; s < 7; s++) {
    if (s == style) continue;
    r -= WEIGHT[program][s];
    if (r < 0) { style = s; break; }
  }
  bool longStyle = (style == BREATH || style == STEPS);
  unsigned long len = longStyle ? random(8000, 10001) : random(6000, 8001);
  styleStart = now;
  styleEnd = now + len;
  tempo = random(85, 116);
  step = 0;
  nextTap = now + GAP_BETWEEN;   // שקט קצר לפני המצב החדש
  tapLen = 0;
}

unsigned long scaled(unsigned long ms) { return ms * tempo / 100; }

// מתזמן את הנגיעה הבאה לפי המצב
void scheduleTap(unsigned long now) {
  int base = TAP_MS[program];
  int power = POWER[program];
  unsigned long gap = 1000;
  tapStart = now;
  tapLen = base;
  tapPower = power;
  tapRamp = false;

  switch (style) {
    case DROPS:                          // נגיעה כל שנייה
      gap = scaled(1000);
      break;
    case BREATH: {                       // הקצב מאיץ ומאט כמו נשימה
      unsigned long x = (now - styleStart) % 8000;
      unsigned long tri = (x < 4000) ? x : 8000 - x;   // 0..4000..0
      gap = scaled(1000 - tri / 8);                     // 1000..500..1000
      break;
    }
    case HEART:                          // טה-טום ... הפסקה
      if (step % 2 == 0) gap = scaled(180);
      else { gap = scaled(950); tapPower = power * 85 / 100; }
      break;
    case ROLL: {                         // שלוש נגיעות שמתחזקות
      int k = step % 3;
      tapLen = base * (6 + k * 4) / 10;                 // 60%, 100%, 140%
      tapPower = power * (75 + k * 12) / 100;
      gap = (k == 2) ? scaled(900) : scaled(200);
      break;
    }
    case STEPS: {                        // 1, 2, 3 נגיעות עם הפסקות
      const int gaps[6] = {700, 180, 700, 180, 180, 900};
      gap = scaled(gaps[step % 6]);
      break;
    }
    case RAIN:                           // נגיעות בזמנים ובחוזק אקראיים
      gap = random(250, 1001);
      tapLen = base * random(70, 131) / 100;
      tapPower = power * random(70, 101) / 100;
      break;
    case PRESS:                          // לחיצה רכה שמתחזקת ונעלמת
      tapRamp = true;
      tapLen = scaled(350);
      gap = tapLen + scaled(1100);
      break;
  }
  nextTap = now + gap;
  step++;
}

// העוצמה של המנוע ברגע הזה
int massageLevel(unsigned long now) {
  if (style < 0 || now >= styleEnd) pickStyle(now);
  if (now >= nextTap) scheduleTap(now);
  unsigned long t = now - tapStart;
  if (tapLen == 0 || t >= tapLen) return 0;
  if (tapRamp) {                                   // עולה ויורד בצורת משולש
    unsigned long half = tapLen / 2;
    unsigned long tri = (t < half) ? t : tapLen - t;
    return 70 + (long)(tapPower - 70) * tri / half;
  }
  if (t < 12) return KICK;                         // דחיפה בהתחלה
  return tapPower;
}

void startProgram(int i, unsigned long elapsed, unsigned long now) {
  if (program != i) randomSeed(micros() ^ analogRead(A6));
  program = i;
  programStart = now - elapsed;
  fading = false;
  pausedProgram = -1;
  style = -1;
}

void startFade(unsigned long now, bool byUser) {
  fadeStart = now;
  fading = true;
  userStopped = byUser;
  pausedElapsed = now - programStart;
}

ISR(PCINT2_vect) {}  // לחיצה על כפתור מעירה את הארדואינו

void goToSleep() {
  analogWrite(MOTOR, 0);
  for (int i = 0; i < 2; i++) {  // שני הבהובים מהירים = הולך לישון
    digitalWrite(LED, HIGH); delay(100);
    digitalWrite(LED, LOW);  delay(100);
  }
  pausedProgram = -1;
  PCMSK2 = bit(PCINT18) | bit(PCINT20) | bit(PCINT23);  // D2, D4, D7
  PCIFR |= bit(PCIF2);
  PCICR |= bit(PCIE2);
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_cpu();
  sleep_disable();
  PCICR &= ~bit(PCIE2);
  lastActivity = millis();
}

void setup() {
  pinMode(MOTOR, OUTPUT);
  pinMode(LED, OUTPUT);
  for (int i = 0; i < 3; i++) pinMode(BUTTON[i], INPUT_PULLUP);
  lastActivity = millis();
}

void loop() {
  unsigned long now = millis();

  for (int i = 0; i < 3; i++) {
    if (!pressed(i)) continue;
    lastActivity = now;
    if (program == i && !fading) {
      startFade(now, true);                    // אותו כפתור = עצירה
    } else if (program == i && fading && userStopped) {
      startProgram(i, pausedElapsed, now);     // לחיצה תוך כדי היחלשות = ממשיך
    } else if (program < 0 && pausedProgram == i) {
      startProgram(i, pausedElapsed, now);     // תוך 20 שניות = ממשיך מאותה נקודה
    } else {
      startProgram(i, 0, now);                 // תוכנית אחרת = מההתחלה
    }
  }

  int level = 0;
  if (program >= 0) {
    if (!fading && now - programStart >= MINUTES[program] * 60000UL) {
      startFade(now, false);                   // התוכנית נגמרה
    }
    level = massageLevel(now);
    if (fading) {
      unsigned long t = now - fadeStart;
      if (t >= FADE_TIME) {
        pausedProgram = userStopped ? program : -1;
        program = -1;
        fading = false;
        lastActivity = now;
        level = 0;
      } else {
        level = (long)level * (FADE_TIME - t) / FADE_TIME;  // הנגיעות נחלשות
      }
    }
  }

  analogWrite(MOTOR, level);
  digitalWrite(LED, program >= 0 ? HIGH : LOW);

  if (program < 0 && now - lastActivity >= IDLE_SLEEP) goToSleep();
}
