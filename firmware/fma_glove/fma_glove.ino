/*
 * FMA Nexus-1 · הקוד המלא לכפפה (Drop 01)
 * לוח: Arduino Nano (ATmega328P). כל ה-GND משותפים.
 *
 * כפתורים (בין הפין ל-GND, בלי נגד):
 *   ירוק D4 · כחול D7 · אדום D2
 *   לחיצה קצרה  = תוכנית עיסוי: ירוק 2 דק', כחול 4 דק', אדום 6 דק'.
 *                 אותו כפתור שוב = עצירה. שוב תוך 20 שניות = ממשיך מאיפה שעצר.
 *   לחיצה ארוכה = חימום (שנייה וחצי): ירוק חלש, כחול בינוני, אדום חזק.
 *                 לחיצה ארוכה על אותו צבע שוב = חימום כבוי.
 *
 * יציאות (כל אחת דרך מודול IRF520, אף פעם לא ישירות מהפין):
 *   D5 מנועי רטט · D9 לוגו (עוצמת האור = רמת החימום) · D6 מחמם
 *
 * חיישן חום NTC 10K (B3950), בצד של העור:
 *   בכפפה: רגל אחת של ה-NTC ל-5V המשותף, הרגל השנייה בחוט ה-NTC של הכבל.
 *   בקופסה: חוט ה-NTC ל-A0, ונגד 10K בין A0 ל-GND.
 *   5V ── NTC ── A0 ── נגד 10K ── GND
 *
 * בטיחות: לא יותר מ-40°C. אם החיישן מנותק או בקצר, החימום נכבה.
 * החימום נכבה לבד אחרי 30 דקות. הכפפה לא נכנסת לשינה כשהחימום דולק.
 *
 * מודול הטעינה מכבה את עצמו כשהעומס קטן (בערך 30 שניות). לכן, כשהמחמם כבוי,
 * הוא נדלק לשליש שנייה פעם ב-15 שניות ("דופק"), גם בזמן שינה. זה 2% מהזמן, בלי חום מורגש.
 * אחרי 10 דקות בלי אף לחיצה (ובלי תוכנית או חימום) הדופק נפסק, המודול נכבה,
 * והסוללה לא מתרוקנת. כדי להדליק שוב: מתג כבוי ואז דולק.
 */

#include <avr/sleep.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <math.h>

#define DEBUG 1      // 1 = מדפיס טמפרטורה ומצב ל-Serial Monitor (9600) פעם בשנייה
#define NTC_TO_5V 1  // 1 = בכפפה (NTC בין 5V ל-A0). ב-Wokwi לשנות ל-0, כי שם החיישן מחובר הפוך

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
const unsigned long LONG_PRESS = 1500;   // לחיצה ארוכה = חימום

// ---- שמירה על מודול הטעינה דולק ----
const unsigned long KEEPALIVE_EVERY = 15000;          // דופק כל 15 שניות
const unsigned long KEEPALIVE_PULSE = 300;            // אורך הדופק
const unsigned long AUTO_OFF = 10UL * 60 * 1000;      // 10 דקות בלי לחיצה = כיבוי מלא
const uint8_t SLEEP_WAKES_PER_PULSE = 2;              // בשינה: התעוררות כל 8 שניות, דופק כל 16

// ---- לוגו ----
const int LOGO = 9;
const uint8_t LOGO_PERCENT[4] = {35, 55, 75, 100};   // חימום: כבוי, חלש, בינוני, חזק
const unsigned long LOGO_FADE = 500;                  // מעבר הדרגתי בין עוצמות

// ---- חימום ----
const int HEATER = 6;
const int NTC_PIN = A0;
const float HEAT_TARGET[4] = {0, 35.0, 38.0, 40.0};   // °C על העור
const float HEAT_HARD_MAX = 40.5;                     // מעל זה המחמם כבוי תמיד
const float HEAT_HYST = 0.5;                          // נדלק שוב רק 0.5° מתחת ליעד
const unsigned long HEAT_MAX_TIME = 30UL * 60 * 1000; // כיבוי אוטומטי אחרי 30 דקות
const float NTC_R_FIXED = 10000.0, NTC_R0 = 10000.0, NTC_T0 = 25.0, NTC_BETA = 3950.0;

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


// ---- כפתורים: לחיצה קצרה או ארוכה ----
bool rawDown[3] = {false, false, false};
bool btnDown[3] = {false, false, false};
bool longDone[3] = {false, false, false};
unsigned long rawChange[3] = {0, 0, 0};
unsigned long downSince[3] = {0, 0, 0};

// 0 = כלום, 1 = לחיצה קצרה (בשחרור), 2 = לחיצה ארוכה (אחרי שנייה וחצי)
int readButton(int i, unsigned long now) {
  bool raw = digitalRead(BUTTON[i]) == LOW;
  if (raw != rawDown[i]) { rawDown[i] = raw; rawChange[i] = now; }
  if (raw != btnDown[i] && now - rawChange[i] >= 30) {
    btnDown[i] = raw;
    if (raw) { downSince[i] = now; longDone[i] = false; }
    else if (!longDone[i]) return 1;
  }
  if (btnDown[i] && !longDone[i] && now - downSince[i] >= LONG_PRESS) {
    longDone[i] = true;
    return 2;
  }
  return 0;
}

// ---- חימום ולוגו ----
int heatLevel = 0;               // 0 כבוי, 1 חלש, 2 בינוני, 3 חזק
unsigned long heatStart = 0;
bool heaterOn = false;
float skinTemp = NAN;
unsigned long lastTempRead = 0;
int logoFrom = 0, logoTo = 0;
unsigned long logoFadeStart = 0;
unsigned long lastKeepAlive = 0;
volatile bool buttonWoke = false;

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

// ---- לוגו ----
int logoPWM(int percent) { return (long)percent * percent * 255 / 10000; }   // נראה לעין יותר אחיד

int logoNow(unsigned long now) {
  unsigned long t = now - logoFadeStart;
  if (t >= LOGO_FADE) return logoTo;
  return logoFrom + (long)(logoTo - logoFrom) * (long)t / (long)LOGO_FADE;
}

void setLogo(int pwm, unsigned long now) {
  logoFrom = logoNow(now);
  logoTo = pwm;
  logoFadeStart = now;
}

// ---- חימום ----
float readTemp() {
  long sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(NTC_PIN);
  int adc = sum / 8;
  if (adc < 15 || adc > 1008) return NAN;          // חיישן בקצר או מנותק
#if NTC_TO_5V
  float r = NTC_R_FIXED * (1023.0 - adc) / adc;    // NTC למעלה (5V), נגד 10K למטה (GND)
#else
  float r = NTC_R_FIXED * adc / (1023.0 - adc);    // NTC למטה (GND), נגד 10K למעלה (5V)
#endif
  float k = 1.0 / (log(r / NTC_R0) / NTC_BETA + 1.0 / (NTC_T0 + 273.15));
  return k - 273.15;
}

bool sensorOk() { return !isnan(skinTemp) && skinTemp > -10 && skinTemp < 80; }

void setHeatLevel(int level, unsigned long now) {
  heatLevel = level;
  heatStart = now;
  lastActivity = now;
  setLogo(logoPWM(LOGO_PERCENT[level]), now);
}

// דופק קצר במחמם, כדי שמודול הטעינה יראה עומס ולא יכבה
bool tooHot() { return sensorOk() && skinTemp >= HEAT_HARD_MAX; }

bool keepAlivePulse(unsigned long now) {
  if (heaterOn) { lastKeepAlive = now; return false; }   // המחמם עצמו כבר עומס
  if (tooHot()) return false;
  if (now - lastKeepAlive >= KEEPALIVE_EVERY) lastKeepAlive = now;
  return now - lastKeepAlive < KEEPALIVE_PULSE;
}

void updateHeat(unsigned long now) {
  if (now - lastTempRead >= 250) { lastTempRead = now; skinTemp = readTemp(); }
  if (heatLevel > 0 && !sensorOk()) setHeatLevel(0, now);                  // תקלה בחיישן = כבוי
  if (heatLevel > 0 && now - heatStart >= HEAT_MAX_TIME) setHeatLevel(0, now);

  if (heatLevel == 0 || !sensorOk() || skinTemp >= HEAT_HARD_MAX) heaterOn = false;
  else if (skinTemp >= HEAT_TARGET[heatLevel]) heaterOn = false;
  else if (skinTemp <= HEAT_TARGET[heatLevel] - HEAT_HYST) heaterOn = true;
  digitalWrite(HEATER, (heaterOn || keepAlivePulse(now)) ? HIGH : LOW);
}

// ---- שינה ----
ISR(PCINT2_vect) { buttonWoke = true; }  // לחיצה על כפתור מעירה את הארדואינו
ISR(WDT_vect) {}                          // שעון ה-watchdog מעיר כל 8 שניות לדופק

void watchdog8s() {
  cli();
  MCUSR &= ~bit(WDRF);
  WDTCSR = bit(WDCE) | bit(WDE);
  WDTCSR = bit(WDIE) | bit(WDP3) | bit(WDP0);   // פסיקה בלבד (בלי ריסט), 8 שניות
  sei();
}

void goToSleep() {
  analogWrite(MOTOR, 0);
  digitalWrite(HEATER, LOW);
  heaterOn = false;
  for (int v = logoNow(millis()); v > 0; v -= 4) { analogWrite(LOGO, v); delay(8); }   // הלוגו נכבה לאט
  analogWrite(LOGO, 0);
  for (int i = 0; i < 2; i++) {  // שני הבהובים מהירים = הולך לישון
    digitalWrite(LED, HIGH); delay(100);
    digitalWrite(LED, LOW);  delay(100);
  }
#if DEBUG
  Serial.println(F("sleep"));
  Serial.flush();
#endif
  pausedProgram = -1;
  unsigned long idle = millis() - lastActivity;   // הזמן מאז הלחיצה האחרונה (millis עוצר בשינה)
  bool fullOff = false;
  uint8_t wakes = 0;
  buttonWoke = false;
  while (!buttonWoke) {
    PCMSK2 = bit(PCINT18) | bit(PCINT20) | bit(PCINT23);  // D2, D4, D7
    PCIFR |= bit(PCIF2);
    PCICR |= bit(PCIE2);
    if (!fullOff) watchdog8s();
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    sleep_cpu();
    sleep_disable();
    wdt_disable();
    PCICR &= ~bit(PCIE2);
    if (buttonWoke) break;
    idle += 8000;                                  // התעורר מה-watchdog
    if (idle >= AUTO_OFF) { fullOff = true; continue; }   // בלי דופק: המודול יכבה את הכול
    if (++wakes >= SLEEP_WAKES_PER_PULSE) {
      wakes = 0;
      skinTemp = readTemp();
      if (!tooHot()) { digitalWrite(HEATER, HIGH); delay(KEEPALIVE_PULSE); digitalWrite(HEATER, LOW); }
    }
  }
  unsigned long now = millis();
  lastKeepAlive = now;
  lastActivity = now;
  logoFrom = 0; logoTo = 0; logoFadeStart = now;
  setLogo(logoPWM(LOGO_PERCENT[heatLevel]), now);       // הלוגו נדלק שוב
}

// ---- תוכנית: לחיצה קצרה ----
void shortPress(int i, unsigned long now) {
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

// ---- חימום: לחיצה ארוכה ----
void longPress(int i, unsigned long now) {
  int level = i + 1;                         // ירוק 1, כחול 2, אדום 3
  setHeatLevel(heatLevel == level ? 0 : level, now);
}

void setup() {
  MCUSR = 0;
  wdt_disable();
  pinMode(MOTOR, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(LOGO, OUTPUT);
  pinMode(HEATER, OUTPUT);
  digitalWrite(HEATER, LOW);
  for (int i = 0; i < 3; i++) pinMode(BUTTON[i], INPUT_PULLUP);
#if DEBUG
  Serial.begin(9600);
#endif
  unsigned long now = millis();
  lastActivity = now;
  skinTemp = readTemp();
  setLogo(logoPWM(LOGO_PERCENT[0]), now);
}

void loop() {
  unsigned long now = millis();

  for (int i = 0; i < 3; i++) {
    int b = readButton(i, now);
    if (b == 0) continue;
    lastActivity = now;
    if (b == 1) shortPress(i, now);
    else longPress(i, now);
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
  updateHeat(now);
  analogWrite(LOGO, logoNow(now));

#if DEBUG
  static unsigned long lastPrint = 0;
  if (now - lastPrint >= 1000) {
    lastPrint = now;
    Serial.print(F("program ")); Serial.print(program);
    Serial.print(F("  heat ")); Serial.print(heatLevel);
    Serial.print(F("  temp "));
    if (sensorOk()) Serial.print(skinTemp, 1); else Serial.print(F("ERR"));
    Serial.print(F("  heater ")); Serial.println(heaterOn ? F("ON") : F("off"));
  }
#endif

  if (program < 0 && heatLevel == 0 && now - lastActivity >= IDLE_SLEEP) goToSleep();
}
