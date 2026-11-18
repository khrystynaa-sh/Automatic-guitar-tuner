#include <Arduino.h>
#include <Preferences.h>
#include "tunings.h"
#include "audio.h"
#include "display.h"
#include "motor.h"
#include "buttons.h"
#include "battery.h"

// ---------- Detection ----------
// Loudness in dBFS: 0 = loudest the mic can record, more negative = quieter.
// The real thresholds are set at startup from the room's noise level.
float thOn  = -55;   // louder than this: a note is playing
float thOff = -65;   // quieter than this: silence
const float NOISE_MARGIN_OFF = 6;    // thOff = noise + 6 dB
const float NOISE_MARGIN_ON  = 14;   // thOn  = noise + 14 dB
const int   CALIBRATE_FRAMES = 25;   // ~1.2 s of silence at startup

// One frame = ~46 ms
const int   CONFIRM_FRAMES = 3;   // same string N frames in a row -> lock it
const int   QUIET_FRAMES   = 6;   // N silent frames -> note has ended
const int   RELOCK_FRAMES  = 5;   // motor off, auto: frames before switching string
const float IN_TUNE_CENTS  = 5;

// ---------- Display ----------
const float    SMOOTH              = 0.35f;
const uint32_t DISPLAY_INTERVAL_MS = 100;

// ---------- Motor tuning loop (calibrate on the real instrument) ----------
const int   MOTOR_DIR          = 1;     // flip to -1 if the motor loosens when it should tighten
const float STEPS_PER_CENT     = 4.0f;  // how far to turn per cent of error
const int   MIN_STEPS          = 16;
const int   MAX_STEPS          = 512;
const int   READINGS_PER_CHECK = 5;     // median of N readings before each correction
const int   IN_TUNE_CHECKS     = 2;     // in tune N checks in a row -> done
const uint32_t SETTLE_MS       = 60;    // extra wait after the motor stops
const int   MAX_ITER           = 30;
const long  MAX_TOTAL_STEPS    = 10L * STEPS_PER_REV;
const float MAX_CORRECT_CENTS  = 700;   // further off than this: don't trust it, don't move
const uint32_t REPLUCK_TIMEOUT_MS = 10000;

// ---------- Battery (INA219) ----------
const uint32_t BATTERY_INTERVAL_MS = 2000;
const float    BATT_WARN_V  = 3.60f;   // show the battery line in yellow
const float    BATT_LOW_V   = 3.40f;   // below this the motor is blocked
const float    BATT_OK_V    = 3.50f;   // motor allowed again above this

const bool DEBUG = true;
const int  DEBUG_EVERY = 4;            // print every Nth frame (~5 lines/s)

// ---------- Settings (saved in flash) ----------
struct Settings {
  uint8_t instr   = 0;
  uint8_t tuning  = 0;
  uint8_t manual  = 0;   // 0 = auto, 1 = manual
  uint8_t motorOn = 1;
  uint8_t string  = 0;   // index into tuning notes, manual mode
} cfg;

Preferences prefs;

const Instrument& instrument() { return INSTRUMENTS[cfg.instr]; }
const Tuning&     tuning()     { return instrument().tunings[cfg.tuning]; }
int stringNum(int idx)         { return tuning().count - idx; }

void loadSettings() {
  prefs.begin("tuner", false);
  cfg.instr   = prefs.getUChar("instr", 0);
  cfg.tuning  = prefs.getUChar("tuning", 0);
  cfg.manual  = prefs.getUChar("manual", 0);
  cfg.motorOn = prefs.getUChar("motor", 1);
  cfg.string  = prefs.getUChar("string", 0);
  if (cfg.instr >= INSTRUMENT_COUNT)       cfg.instr = 0;
  if (cfg.tuning >= instrument().count)    cfg.tuning = 0;
  if (cfg.string >= tuning().count)        cfg.string = 0;
  cfg.manual  = cfg.manual ? 1 : 0;
  cfg.motorOn = cfg.motorOn ? 1 : 0;
}

void saveSettings() {
  prefs.putUChar("instr", cfg.instr);
  prefs.putUChar("tuning", cfg.tuning);
  prefs.putUChar("manual", cfg.manual);
  prefs.putUChar("motor", cfg.motorOn);
  prefs.putUChar("string", cfg.string);
}

void applySettings() {
  float lo = 1e9, hi = 0;
  for (int i = 0; i < tuning().count; i++) {
    float f = noteFreq(tuning().notes[i]);
    lo = min(lo, f);
    hi = max(hi, f);
  }
  audioSetRange(lo * 0.8f, hi * 1.4f);
  Serial.printf("Settings: %s / %s / %s / motor %s  (search %.0f-%.0f Hz)\n",
                instrument().name, tuning().name, cfg.manual ? "MANUAL" : "AUTO",
                cfg.motorOn ? "ON" : "OFF", lo * 0.8f, hi * 1.4f);
}

// ---------- Tuning state ----------
enum Phase { IDLE, LOCKED, SETTLE, DONE };
const char* PHASE_NAMES[] = {"IDLE", "LOCKED", "SETTLE", "DONE"};
Phase phase = IDLE;

int   target = -1;                  // locked string index
int   candString = -1, candCount = 0;
int   relockCand = -1, relockCount = 0;
int   quietCount = 0;
uint32_t silentSince = 0;

float smoothCents = 0, smoothF = 0;
bool  haveSmooth = false;
uint32_t lastDraw = 0;

float readings[READINGS_PER_CHECK];
int   nReadings = 0, inTuneChecks = 0, iterations = 0;
long  totalSteps = 0;

const char* doneMsg = "";
uint16_t    doneColor = COL_GREEN;

bool inMenu = false;
int  menuItem = 0;

// ---------- Battery ----------
bool     haveBattery = false;
bool     batteryLow  = false;
uint32_t lastBattery = 0;

// Read the INA219 every few seconds and redraw the bottom line.
void updateBattery(bool force) {
  if (!haveBattery) return;
  if (!force && millis() - lastBattery < BATTERY_INTERVAL_MS) return;
  lastBattery = millis();

  BatteryInfo b;
  if (!batteryRead(b)) return;

  char buf[40];
  uint16_t color = COL_GREY;
  if (!b.onBattery) {
    batteryLow = false;
    snprintf(buf, sizeof(buf), "No battery (USB power)");
  } else {
    // Voltage sags while the motor runs, so only judge it when idle.
    if (!motorBusy()) {
      if (b.volts < BATT_LOW_V)     batteryLow = true;
      else if (b.volts > BATT_OK_V) batteryLow = false;
    }
    snprintf(buf, sizeof(buf), "Battery %.2fV  %d%%  %.0fmA", b.volts, b.percent, b.mA);
    if (batteryLow)                  color = COL_RED;
    else if (b.volts < BATT_WARN_V)  color = COL_YELLOW;
  }
  displayBattery(buf, color);
  if (DEBUG) Serial.printf("Battery: %.3f V  %.1f mA  %d%%%s\n",
                           b.volts, b.mA, b.percent, batteryLow ? "  LOW" : "");
}

// ---------- Screens ----------
void showIdleScreen() {
  static char buf[24];
  if (cfg.manual) {
    displayNote(tuning().notes[cfg.string].name, stringNum(cfg.string));
    snprintf(buf, sizeof(buf), "Play string %d", stringNum(cfg.string));
    displayIdle(buf);
  } else {
    displayClearNote();
    displayIdle("Play a string");
  }
}

void drawMainScreen() {
  displayClearAll();
  displayHeader(instrument().name, tuning().name, cfg.manual, cfg.motorOn);
  showIdleScreen();
  updateBattery(true);
}

void resetSession() {
  phase = IDLE;
  target = -1;
  candString = -1; candCount = 0;
  relockCand = -1; relockCount = 0;
  quietCount = 0; silentSince = 0;
  nReadings = 0; inTuneChecks = 0; iterations = 0; totalSteps = 0;
  haveSmooth = false;
  showIdleScreen();
}

void lockTarget(int idx) {
  target = idx;
  phase = LOCKED;
  relockCand = -1; relockCount = 0;
  nReadings = 0; inTuneChecks = 0; iterations = 0; totalSteps = 0;
  haveSmooth = false;
  lastDraw = 0;
  const Note& n = tuning().notes[idx];
  Serial.printf("Locked string %d (%s, %.2f Hz), motor %s\n",
                stringNum(idx), n.name, noteFreq(n), cfg.motorOn ? "ON" : "OFF");
}

void finish(const char* msg, uint16_t color) {
  motorStop();
  phase = DONE;
  doneMsg = msg;
  doneColor = color;
  displayStatus(msg, color);
  Serial.printf("Finished: %s after %d moves, %ld steps\n", msg, iterations, totalSteps);
}

// ---------- Menu ----------
const int MENU_ITEMS = 5;
const char* const MENU_LABELS[MENU_ITEMS] = {"Instr", "Tuning", "Mode", "Motor", "Test"};

int wrap(int v, int n) { return (v % n + n) % n; }

void drawMenuRows() {
  const char* values[MENU_ITEMS] = {
    instrument().name,
    tuning().name,
    cfg.manual ? "Manual" : "Auto",
    cfg.motorOn ? "On" : "Off",
    "< > 1/4 turn",
  };
  displayMenuRows(menuItem, MENU_LABELS, values, MENU_ITEMS);
}

void changeMenuValue(int delta) {
  switch (menuItem) {
    case 0:
      cfg.instr = wrap(cfg.instr + delta, INSTRUMENT_COUNT);
      cfg.tuning = 0;
      cfg.string = 0;
      break;
    case 1:
      cfg.tuning = wrap(cfg.tuning + delta, instrument().count);
      if (cfg.string >= tuning().count) cfg.string = 0;
      break;
    case 2: cfg.manual  = !cfg.manual;  break;
    case 3: cfg.motorOn = !cfg.motorOn; break;
    case 4:   // motor test: quarter turn either way, ignores all tuning logic
      Serial.printf("Motor test: %+d steps\n", delta * STEPS_PER_REV / 4);
      motorMove(delta * STEPS_PER_REV / 4);
      break;
  }
}

void onMenuButton(Button b) {
  switch (b) {
    case BTN_MENU: menuItem = (menuItem + 1) % MENU_ITEMS; drawMenuRows(); break;
    case BTN_PREV: changeMenuValue(-1); drawMenuRows(); break;
    case BTN_NEXT: changeMenuValue(+1); drawMenuRows(); break;
    case BTN_OK:
      motorStop();
      saveSettings();
      applySettings();
      inMenu = false;
      drawMainScreen();
      resetSession();
      break;
    default: break;
  }
}

void onMainButton(Button b) {
  switch (b) {
    case BTN_MENU:
      motorStop();
      inMenu = true;
      menuItem = 0;
      displayMenuOpen();
      drawMenuRows();
      updateBattery(true);
      break;
    case BTN_OK:   // emergency stop
      motorStop();
      if (cfg.motorOn && (phase == LOCKED || phase == SETTLE)) finish("STOPPED", COL_RED);
      break;
    case BTN_PREV:
    case BTN_NEXT:
      if (!cfg.manual) break;
      motorStop();
      cfg.string = wrap(cfg.string + (b == BTN_NEXT ? 1 : -1), tuning().count);
      saveSettings();
      resetSession();
      break;
    default: break;
  }
}

void handleButtons() {
  Button b;
  while ((b = buttonsGet()) != BTN_NONE) {
    if (DEBUG) Serial.printf("Button %s\n", buttonName(b));
    if (inMenu) onMenuButton(b);
    else        onMainButton(b);
  }
}

// ---------- Tuning logic ----------
int nearestString(float f) {
  int best = 0;
  float bestC = 1e9;
  for (int i = 0; i < tuning().count; i++) {
    float c = 1200.0f * log2f(f / noteFreq(tuning().notes[i]));
    if (fabsf(c) < fabsf(bestC)) { bestC = c; best = i; }
  }
  return best;
}

float median(const float* v, int n) {
  float t[8];
  for (int i = 0; i < n; i++) t[i] = v[i];
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && t[j - 1] > t[j]; j--) { float s = t[j]; t[j] = t[j - 1]; t[j - 1] = s; }
  return t[n / 2];
}

void showLive(float cents) {
  if (millis() - lastDraw < DISPLAY_INTERVAL_MS) return;
  lastDraw = millis();

  const Note& n = tuning().notes[target];
  displayNote(n.name, stringNum(target));
  bool ok = fabsf(smoothCents) < IN_TUNE_CENTS;
  displayReading(smoothF, smoothCents, ok ? COL_GREEN : COL_RED);

  if (phase == DONE)                                        displayStatus(doneMsg, doneColor);
  else if (ok)                                              displayStatus("IN TUNE", COL_GREEN);
  else if (cfg.motorOn && fabsf(cents) > MAX_CORRECT_CENTS) displayStatus("Too far - by hand", COL_RED);
  else if (cfg.motorOn)  displayStatus(cents < 0 ? "LOW - tuning up" : "HIGH - tuning dn", COL_RED);
  else                   displayStatus(cents < 0 ? "LOW - tighten" : "HIGH - loosen", COL_RED);
}

// Collect readings; once there are enough, either finish or turn the peg.
void tuningStep(float cents) {
  if (fabsf(cents) > MAX_CORRECT_CENTS) {
    nReadings = 0;
    if (DEBUG) Serial.printf("  %+.0f c is too far for the motor\n", cents);
    return;
  }
  readings[nReadings++] = cents;
  if (nReadings < READINGS_PER_CHECK) return;
  nReadings = 0;

  float m = median(readings, READINGS_PER_CHECK);
  if (fabsf(m) < IN_TUNE_CENTS) {
    if (++inTuneChecks >= IN_TUNE_CHECKS) finish("TUNED!", COL_GREEN);
    return;
  }
  inTuneChecks = 0;

  if (iterations >= MAX_ITER || totalSteps >= MAX_TOTAL_STEPS) {
    finish("LIMIT - stopped", COL_RED);
    return;
  }
  if (batteryLow) {
    finish("Charge battery", COL_RED);
    return;
  }

  int steps = constrain((int)(fabsf(m) * STEPS_PER_CENT), MIN_STEPS, MAX_STEPS);
  int dir = (m < 0 ? 1 : -1) * MOTOR_DIR;   // flat -> tighten, sharp -> loosen
  motorMove(dir * steps);
  iterations++;
  totalSteps += steps;
  phase = SETTLE;

  char buf[24];
  snprintf(buf, sizeof(buf), "MOTOR %+d", dir * steps);
  displayStatus(buf, COL_CYAN);
  Serial.printf("  move %d: %+.1f c -> %+d steps\n", iterations, m, dir * steps);
}

void handleQuiet() {
  if (phase == IDLE) {
    candString = -1; candCount = 0;
    return;
  }
  bool tuningActive = cfg.motorOn && phase == LOCKED;
  if (!tuningActive) {
    if (++quietCount >= QUIET_FRAMES) resetSession();
    return;
  }
  // Note died out before the string was in tune: wait for another pluck.
  if (++quietCount == QUIET_FRAMES) {
    silentSince = millis();
    nReadings = 0;
    displayStatus("Pluck again", COL_YELLOW);
  }
  if (silentSince && millis() - silentSince > REPLUCK_TIMEOUT_MS) resetSession();
}

void processFrame(const AudioFrame& fr) {
  bool busy = motorBusy();
  if (DEBUG) {
    static int dbg = 0;
    if (++dbg % DEBUG_EVERY == 0)
      Serial.printf("dB:%6.1f  f:%6.1f  clarity:%.2f  phase:%s  target:%d  moves:%d  busy:%d\n",
                    fr.dbfs, fr.freq, fr.clarity, PHASE_NAMES[phase],
                    target >= 0 ? stringNum(target) : 0, iterations, busy);
  }

  // Ignore audio recorded while the peg was moving: the motor is noisy
  // and the string's pitch was changing.
  if (busy) return;
  if ((int32_t)(fr.t - motorStoppedAt()) < AUDIO_WINDOW_MS + (int32_t)SETTLE_MS) return;

  if (phase == SETTLE) {
    phase = LOCKED;
    nReadings = 0;
    haveSmooth = false;
  }

  // Once a string is locked, keep listening down to the quieter threshold.
  float gate = (phase == IDLE) ? thOn : thOff;
  float f = (fr.dbfs > gate) ? fr.freq : 0;

  if (f > 0) {
    quietCount = 0;
    silentSince = 0;
    int idx = cfg.manual ? cfg.string : nearestString(f);

    if (phase == IDLE) {
      if (idx == candString) candCount++;
      else { candString = idx; candCount = 1; }
      if (candCount < CONFIRM_FRAMES) return;
      lockTarget(idx);
    } else if (!cfg.motorOn && !cfg.manual && idx != target) {
      // Pure tuner mode: follow the player to another string.
      if (idx == relockCand) relockCount++;
      else { relockCand = idx; relockCount = 1; }
      if (relockCount >= RELOCK_FRAMES) lockTarget(idx);
    } else {
      relockCount = 0;
    }

    float cents = 1200.0f * log2f(f / noteFreq(tuning().notes[target]));
    if (!haveSmooth) { smoothCents = cents; smoothF = f; haveSmooth = true; }
    else {
      smoothCents += SMOOTH * (cents - smoothCents);
      smoothF     += SMOOTH * (f - smoothF);
    }
    showLive(cents);

    if (phase == LOCKED && cfg.motorOn) tuningStep(cents);
  } else if (fr.dbfs < thOff) {
    handleQuiet();
  }
}

// Measure the room's background noise and set the thresholds above it.
void calibrateNoise() {
  displayStatus("Quiet please...", COL_YELLOW);
  float db[CALIBRATE_FRAMES];
  int n = 0;
  AudioFrame fr;
  while (n < CALIBRATE_FRAMES) {
    if (audioGetFrame(fr, 200)) db[n++] = fr.dbfs;
  }
  // Sort and take the median, so one bump or click doesn't matter.
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && db[j - 1] > db[j]; j--) { float s = db[j]; db[j] = db[j - 1]; db[j - 1] = s; }
  float noise = db[n / 2];
  thOff = constrain(noise + NOISE_MARGIN_OFF, -75.0f, -45.0f);
  thOn  = constrain(noise + NOISE_MARGIN_ON,  -67.0f, -37.0f);
  Serial.printf("Noise floor %.1f dB -> thresholds: on %.1f, off %.1f\n", noise, thOn, thOff);
}

// ---------- Arduino entry points ----------
void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);   // never stall the program if no one is reading serial
  motorInit();
  displayInit();
  buttonsInit();
  haveBattery = batteryInit();
  loadSettings();
  applySettings();
  drawMainScreen();
  audioInit();
  calibrateNoise();
  resetSession();
  Serial.println("Tuner started");
}

void loop() {
  handleButtons();
  updateBattery(false);

  AudioFrame fr;
  if (!audioGetFrame(fr, 10)) return;   // wait at most 10 ms, then check buttons again
  if (inMenu) return;
  processFrame(fr);
}
