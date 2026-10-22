#include <avr/sleep.h>

const int MOTOR = 5;
const int LED = 13;  // נורית L: דולקת כשתוכנית פועלת

// ---- שלוש התוכניות: כפתור, אורך, עוצמה מקסימלית (עד 160) ----
const int BUTTON[3]             = {4, 7, 2};   // ירוק, כחול, אדום
const unsigned long MINUTES[3]  = {5, 10, 15};
const int STRENGTH[3]           = {110, 135, 160};

const int MIN_LEVEL = 65;               // מתחת לזה המנוע לא מסתובב
const unsigned long STYLE_TIME = 20000; // כל 20 שניות סגנון עיסוי אחר
const unsigned long FADE_TIME = 3000;   // היחלשות בסוף: 3 שניות
const unsigned long IDLE_SLEEP = 20000; // שינה אחרי 20 שניות בלי כלום

int program = -1;          // -1 = אין תוכנית פועלת
bool fading = false;
bool userStopped = false;  // העצירה הייתה בלחיצה (ולא כי התוכנית נגמרה)
int pausedProgram = -1;    // תוכנית שנעצרה ואפשר להמשיך אותה
unsigned long pausedElapsed = 0;
unsigned long programStart = 0, fadeStart = 0, lastActivity = 0;
int level = 0, fadeFrom = 0;

bool lastState[3] = {HIGH, HIGH, HIGH};
unsigned long lastChange[3] = {0, 0, 0};

// מחזיר true פעם אחת בכל לחיצה
bool pressed(int i) {
  bool now = digitalRead(BUTTON[i]);
  if (now != lastState[i] && millis() - lastChange[i] > 50) {
    lastChange[i] = millis();
    lastState[i] = now;
    if (now == LOW) return true;
  }
  return false;
}

// ---- סגנונות העיסוי. כל אחד מחזיר עוצמה בין 0 ל-100 (או -1 = הפסקה) ----
int styleGentle(unsigned long t) {   // רטט עדין מאוד שעולה ויורד לאט
  int x = t % 6000;
  int tri = (x < 3000) ? x : 6000 - x;   // 0..3000..0
  return 5 + tri / 120;                  // 5..30
}
int styleWave(unsigned long t) {     // גל: מתחזק ונחלש בהדרגה
  int x = t % 4000;
  int tri = (x < 2000) ? x : 4000 - x;   // 0..2000..0
  return 20 + tri / 25;                  // 20..100
}
int styleKnead(unsigned long t) {    // לחיצות: עולה, מחזיק, משחרר, נח
  int x = t % 1800;
  if (x < 700)  return 30 + x / 10;      // עולה ל-100
  if (x < 1000) return 100;              // מחזיק
  if (x < 1300) return 50;               // משחרר
  return -1;                             // נח
}
int styleHeartbeat(unsigned long t) {// דופק: טה-טום... הפסקה
  int x = t % 1100;
  if (x < 150) return 100;
  if (x < 250) return -1;
  if (x < 400) return 80;
  return -1;
}
int styleRipple(unsigned long t) {   // אדוות מהירות
  int x = t % 900;
  return 25 + x / 12;                    // 25..100 ושוב מהתחלה
}
int styleTap(unsigned long t) {      // תיפוף: 5 נגיעות קצרות ואז הפסקה
  int x = t % 1600;
  if (x >= 1000) return -1;
  return (x % 200 < 80) ? 90 : -1;
}

// הסדר שבו הסגנונות מתחלפים (העדין חוזר בין הסגנונות החזקים)
const int ORDER[8] = {0, 1, 2, 0, 3, 4, 0, 5};

int massageLevel(unsigned long t, int top) {
  int style = ORDER[(t / STYLE_TIME) % 8];
  int p;
  if (style == 0)      p = styleGentle(t);
  else if (style == 1) p = styleWave(t);
  else if (style == 2) p = styleKnead(t);
  else if (style == 3) p = styleHeartbeat(t);
  else if (style == 4) p = styleRipple(t);
  else                 p = styleTap(t);
  if (p < 0) return 0;
  return MIN_LEVEL + (long)(top - MIN_LEVEL) * p / 100;
}

void startProgram(int i, unsigned long elapsed, unsigned long now) {
  program = i;
  programStart = now - elapsed;
  fading = false;
  pausedProgram = -1;
}

void startFade(unsigned long now, bool byUser) {
  fadeFrom = (level > 0) ? level : MIN_LEVEL;
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
  pausedProgram = -1;            // אחרי שינה מתחילים תוכנית מההתחלה
  PCMSK2 = bit(PCINT18) | bit(PCINT20) | bit(PCINT23);  // D2, D4, D7
  PCIFR |= bit(PCIF2);
  PCICR |= bit(PCIE2);
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_cpu();
  // ...ממשיכים מכאן אחרי לחיצה
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
      startProgram(i, 0, now);                 // תוכנית אחרת = מתחילה מההתחלה
    }
  }

  if (program >= 0 && !fading) {
    unsigned long t = now - programStart;
    if (t >= MINUTES[program] * 60000UL) startFade(now, false);  // התוכנית נגמרה
    else level = massageLevel(t, STRENGTH[program]);
  }
  if (program >= 0 && fading) {
    unsigned long t = now - fadeStart;
    if (t >= FADE_TIME) {
      pausedProgram = userStopped ? program : -1;  // אפשר להמשיך רק אם עצרו בלחיצה
      program = -1;
      fading = false;
      lastActivity = now;
    } else {
      level = fadeFrom - (long)fadeFrom * t / FADE_TIME;
    }
  }
  if (program < 0) level = 0;

  analogWrite(MOTOR, level);
  digitalWrite(LED, program >= 0 ? HIGH : LOW);

  if (program < 0 && now - lastActivity >= IDLE_SLEEP) goToSleep();
}
