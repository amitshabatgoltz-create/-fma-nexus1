#include <avr/sleep.h>
#include <avr/pgmspace.h>

const int MOTOR = 5;
const int LED = 13;  // נורית L: דולקת כשתוכנית פועלת

// ---- שלוש התוכניות: ירוק, כחול, אדום ----
const int BUTTON[3]            = {4, 7, 2};
const unsigned long SECONDS[3] = {120, 240, 360};   // 2, 4, 6 דקות
const int POWER[3]             = {125, 115, 105};   // עוצמת השיא של כל תוכנית (עד 160)
const int ENTRY_DROP = 20;   // בכניסה וביציאה העוצמה נמוכה ב-20 מהשיא
const int SURGE = 10;        // הגל באמצע השיא: עוד 10 מעל השיא
const unsigned long FINALE_TIME = 5000;  // הגמר: 5 השניות האחרונות

const int MIN_LEVEL = 65;                // מתחת לזה המנוע לא מסתובב
const int KICK = 160;                    // דחיפה קצרה בתחילת כל רטט
const unsigned long GAP_BETWEEN = 500;   // שקט קצר בין מצב למצב
const unsigned long FADE_TIME = 3000;    // היחלשות בסוף
const unsigned long IDLE_SLEEP = 20000;  // שינה אחרי 20 שניות בלי כלום

// ---- המצבים ----
// כל מצב הוא רשימה של קטעים: {זמן במילישניות, עוצמה בהתחלה, עוצמה בסוף}
// עוצמה 0-100 (0 = הפסקה). אם ההתחלה והסוף שונים, העוצמה עולה/יורדת בהדרגה.
struct Seg { uint16_t ms; uint8_t from; uint8_t to; };

// ===== קל-בינוני =====
const Seg P_ROLL[]  PROGMEM = {{100,70,70},{150,0,0},{150,85,85},{150,0,0},{250,100,100},{700,0,0}};
const Seg P_STEPS[] PROGMEM = {{120,90,90},{600,0,0},{120,90,90},{150,0,0},{120,90,90},{600,0,0},
                               {120,90,90},{150,0,0},{120,90,90},{150,0,0},{120,90,90},{900,0,0}};
const Seg P_RAIN[]  PROGMEM = {{1,0,0}};   // נוצר באקראי בזמן אמת
const Seg P_PRESS[] PROGMEM = {{250,40,100},{250,100,40},{1000,0,0}};
const Seg P_HEART[] PROGMEM = {{250,100,100},{150,0,0},{250,85,85},{600,0,0}};
const Seg P_ZIGZAG[] PROGMEM = {{200,90,90},{300,0,0},{600,100,100},{300,0,0}};
// ===== אגרסיבי =====
const Seg P_HAMMER[] PROGMEM = {{700,100,100},{400,0,0}};
const Seg P_KNEAD[]  PROGMEM = {{1000,100,100},{300,0,0},{600,75,75},{300,0,0}};
const Seg P_WAVE[]   PROGMEM = {{1500,30,100},{500,100,100},{1000,100,30},{400,0,0}};
const Seg P_SHOCK[]  PROGMEM = {{120,100,100},{100,0,0},{120,100,100},{100,0,0},{800,100,100},{500,0,0}};
const Seg P_BURSTS[] PROGMEM = {{400,90,90},{200,0,0},{400,90,90},{200,0,0},{400,90,90},{200,0,0},
                                {1200,100,100},{500,0,0}};
const Seg P_ESCAL[]  PROGMEM = {{300,100,100},{300,0,0},{500,100,100},{300,0,0},{700,100,100},{300,0,0},
                                {900,100,100},{600,0,0}};
const Seg P_THUNDER[] PROGMEM = {{100,100,100},{100,60,60},{100,100,100},{100,60,60},{100,100,100},
                                 {100,60,60},{100,100,100},{100,60,60},{100,100,100},{100,60,60},
                                 {100,100,100},{100,60,60},{100,100,100},{100,60,60},{100,100,100},
                                 {100,60,60},{100,100,100},{100,60,60},{100,100,100},{100,60,60},{500,0,0}};
const Seg P_SEWING[] PROGMEM = {{150,100,100},{100,0,0},{150,100,100},{100,0,0},{150,100,100},{100,0,0},
                                {150,100,100},{100,0,0},{150,100,100},{100,0,0},{150,100,100},{100,0,0},
                                {150,100,100},{100,0,0},{150,100,100},{600,0,0}};
const Seg P_BREATH[] PROGMEM = {{2000,20,100},{2000,100,20},{300,0,0}};
const Seg P_SOS[]    PROGMEM = {{200,100,100},{200,0,0},{200,100,100},{200,0,0},{200,100,100},{400,0,0},
                                {700,100,100},{250,0,0},{700,100,100},{250,0,0},{700,100,100},{400,0,0},
                                {200,100,100},{200,0,0},{200,100,100},{200,0,0},{200,100,100},{800,0,0}};
const Seg P_PUMP[]   PROGMEM = {{500,100,100},{500,60,60},{500,100,100},{500,60,60},{500,100,100},
                                {500,60,60},{500,0,0}};
// ===== גמר: 3 פטישים ורטט שנחלש =====
const Seg P_FINALE[] PROGMEM = {{700,100,100},{400,0,0},{700,100,100},{400,0,0},{700,100,100},{400,0,0},
                                {1500,80,20},{400,0,0}};

#define N(a) (sizeof(a) / sizeof(a[0]))
const Seg* const PATTERN[18] = {P_ROLL, P_STEPS, P_RAIN, P_PRESS, P_HEART, P_ZIGZAG,
                                P_HAMMER, P_KNEAD, P_WAVE, P_SHOCK, P_BURSTS, P_ESCAL,
                                P_THUNDER, P_SEWING, P_BREATH, P_SOS, P_PUMP, P_FINALE};
const uint8_t PATTERN_LEN[18] = {N(P_ROLL), N(P_STEPS), N(P_RAIN), N(P_PRESS), N(P_HEART), N(P_ZIGZAG),
                                 N(P_HAMMER), N(P_KNEAD), N(P_WAVE), N(P_SHOCK), N(P_BURSTS), N(P_ESCAL),
                                 N(P_THUNDER), N(P_SEWING), N(P_BREATH), N(P_SOS), N(P_PUMP), N(P_FINALE)};
const int RAIN = 2;
const int FINALE = 17;

// הסיכוי של כל מצב בכל תוכנית
//          | קל-בינוני: גלגול מדרגות גשם לחיצה דופק זיגזג | אגרסיבי: פטיש לישה גל הלם סדרות מדרגות-עולות רעם תפירה נשימה SOS משאבה
const uint8_t WEIGHT[3][17] = {
  /* ירוק */ {10, 10, 10, 10, 10, 10,   4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3},
  /* כחול */ { 6,  6,  6,  6,  6,  6,   6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6},
  /* אדום */ { 3,  3,  2,  2,  3,  2,   8, 8, 8, 8, 8, 7, 8, 8, 7, 8, 7}};
// בכניסה וביציאה (בכל התוכניות): בעיקר קל-בינוני
const uint8_t CALM_WEIGHT[17] = {10, 10, 10, 10, 10, 10,   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

// ---- מצב התוכנית ----
int program = -1;
bool fading = false, userStopped = false;
int pausedProgram = -1;
unsigned long pausedElapsed = 0;
unsigned long programStart = 0, fadeStart = 0, lastActivity = 0;

// ---- המצב הנוכחי ----
int style = -1;
unsigned long styleEnd = 0;
int tempo = 100;                 // שינוי קטן אקראי בקצב (85%-115%)
int power = 0;                   // העוצמה של התוכנית ברגע הזה (משתנה לפי השלב)
int seg = 0;                     // הקטע הנוכחי בתוך המצב
unsigned long segStart = 0, segLen = 0;
uint8_t segFrom = 0, segTo = 0;
bool prevWasOff = true;          // הקטע הקודם היה הפסקה (בשביל הדחיפה)

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

unsigned long cycleLength(int s) {
  unsigned long total = 0;
  for (int i = 0; i < PATTERN_LEN[s]; i++) total += pgm_read_word(&PATTERN[s][i].ms);
  return total;
}

// טוען את הקטע הבא של המצב
void loadSegment(unsigned long now) {
  prevWasOff = (segTo == 0 && segFrom == 0);
  if (style == RAIN) {                       // גשם: נגיעות וזמנים אקראיים
    if (seg % 2 == 0) { segLen = random(80, 251); segFrom = segTo = random(70, 101); }
    else              { segLen = random(200, 801); segFrom = segTo = 0; }
  } else {
    const Seg* p = &PATTERN[style][seg % PATTERN_LEN[style]];
    segLen  = (unsigned long)pgm_read_word(&p->ms) * tempo / 100;
    segFrom = pgm_read_byte(&p->from);
    segTo   = pgm_read_byte(&p->to);
  }
  segStart = now;
  seg++;
}

// בוחר מצב אקראי לפי הסיכויים, אף פעם לא אותו מצב פעמיים ברצף
void pickStyle(unsigned long now, bool calm) {
  const uint8_t* w = calm ? CALM_WEIGHT : WEIGHT[program];
  int total = 0;
  for (int s = 0; s < 17; s++) if (s != style) total += w[s];
  int r = random(total);
  for (int s = 0; s < 17; s++) {
    if (s == style) continue;
    r -= w[s];
    if (r < 0) { style = s; break; }
  }
  bool longStyle = (style != RAIN && cycleLength(style) > 3000);
  styleEnd = now + GAP_BETWEEN + (longStyle ? random(8000, 10001) : random(6000, 8001));
  tempo = random(85, 116);
  seg = 0;
  segStart = now; segLen = GAP_BETWEEN; segFrom = segTo = 0;   // שקט קצר לפני המצב החדש
}

void startFinale(unsigned long now) {
  style = FINALE;
  styleEnd = 0xFFFFFFFFUL;   // נמשך עד סוף התוכנית
  tempo = 100;
  seg = 0;
  segStart = now; segLen = 1; segFrom = segTo = 0;
}

// עוצמת התוכנית לפי השלב: כניסה (20%) -> שיא עם גל באמצע (60%) -> יציאה (20%)
int phasePower(unsigned long elapsed, unsigned long total) {
  long top = POWER[program];
  long f = (long)(elapsed * 1000UL / total);           // 0..1000
  if (f < 200) return top - ENTRY_DROP + ENTRY_DROP * f / 200;
  if (f < 800) {
    long d = f - 500; if (d < 0) d = -d;                 // מרחק מאמצע התוכנית
    if (d < 100) return top + SURGE * (100 - d) / 100;   // הגל באמצע השיא
    return top;
  }
  return top - ENTRY_DROP * (f - 800) / 200;
}

int toPWM(int percent) {
  if (percent <= 0) return 0;
  return MIN_LEVEL + (long)(power - MIN_LEVEL) * percent / 100;
}

// העוצמה של המנוע ברגע הזה
int massageLevel(unsigned long now) {
  unsigned long total = SECONDS[program] * 1000UL;
  unsigned long elapsed = now - programStart;
  if (elapsed > total) elapsed = total;
  if (elapsed + FINALE_TIME >= total) {
    if (style != FINALE) startFinale(now);
    power = POWER[program];
  } else {
    power = phasePower(elapsed, total);
    long f = (long)(elapsed * 1000UL / total);
    if (style < 0 || now >= styleEnd) pickStyle(now, f < 200 || f >= 800);
  }
  while (now - segStart >= segLen) loadSegment(segStart + segLen);
  unsigned long t = now - segStart;
  int percent = segFrom + ((long)segTo - segFrom) * (long)t / (long)segLen;
  if (percent <= 0) return 0;
  if (prevWasOff && t < 12) return KICK;     // דחיפה בתחילת רטט
  return toPWM(percent);
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
    if (!fading && now - programStart >= SECONDS[program] * 1000UL) {
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
        level = (long)level * (FADE_TIME - t) / FADE_TIME;  // נחלש בהדרגה
      }
    }
  }

  analogWrite(MOTOR, level);
  digitalWrite(LED, program >= 0 ? HIGH : LOW);

  if (program < 0 && now - lastActivity >= IDLE_SLEEP) goToSleep();
}
