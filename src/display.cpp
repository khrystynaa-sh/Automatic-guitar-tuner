#include "display.h"
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#define TFT_SCL  16
#define TFT_SDA  15
#define TFT_RST  7
#define TFT_DC   6
#define TFT_CS   5
#define TFT_BL   4

static Adafruit_ST7789 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);

static const char* lastNoteName = nullptr;
static int         lastNoteNum  = -1;
static char        lastStatus[24] = "";
static uint16_t    lastStatusColor = 0;

// Each character is 6*size pixels wide, so half of it is 3*size.
static void centerText(const char* s, int y, int size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color);
  tft.setCursor(120 - (int)strlen(s) * 3 * size, y);
  tft.print(s);
}

static void resetCache() {
  lastNoteName = nullptr;
  lastNoteNum = -1;
  lastStatus[0] = '\0';
}

static void drawScale(int cents, bool showNeedle, uint16_t color) {
  tft.fillRect(0, 148, 240, 56, COL_BLACK);
  tft.drawFastHLine(10, 176, 220, COL_WHITE);
  for (int c = -50; c <= 50; c += 10) {
    int xx = 120 + c * 22 / 10;
    tft.drawFastVLine(xx, 170, 13, c == 0 ? COL_GREEN : COL_WHITE);
  }
  if (showNeedle) {
    int xx = 120 + constrain(cents, -50, 50) * 22 / 10;
    tft.fillRect(xx - 3, 150, 6, 52, color);
  }
}

void displayInit() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  SPI.begin(TFT_SCL, -1, TFT_SDA, TFT_CS);
  tft.init(240, 240);
  tft.setSPISpeed(40000000);
  tft.setRotation(2);
  displayClearAll();
}

void displayClearAll() {
  tft.fillScreen(COL_BLACK);
  resetCache();
}

void displayHeader(const char* instrument, const char* tuning, bool manual, bool motorOn) {
  char buf[40];
  tft.fillRect(0, 0, 240, 28, COL_BLACK);
  snprintf(buf, sizeof(buf), "%s  %s", instrument, manual ? "MANUAL" : "AUTO");
  centerText(buf, 2, 2, COL_YELLOW);
  snprintf(buf, sizeof(buf), "%s  |  motor %s", tuning, motorOn ? "ON" : "OFF");
  centerText(buf, 19, 1, COL_GREY);
}

void displayNote(const char* name, int stringNum) {
  if (name == lastNoteName && stringNum == lastNoteNum) return;
  tft.fillRect(0, 28, 240, 90, COL_BLACK);
  centerText(name, 32, 7, COL_WHITE);
  char buf[16];
  snprintf(buf, sizeof(buf), "String %d", stringNum);
  centerText(buf, 96, 2, COL_WHITE);
  lastNoteName = name;
  lastNoteNum = stringNum;
}

void displayClearNote() {
  tft.fillRect(0, 28, 240, 90, COL_BLACK);
  lastNoteName = nullptr;
  lastNoteNum = -1;
}

void displayReading(float freq, float cents, uint16_t needleColor) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%.1f Hz  %+.0f c", freq, cents);
  tft.fillRect(0, 122, 240, 20, COL_BLACK);
  centerText(buf, 124, 2, COL_WHITE);
  drawScale((int)cents, true, needleColor);
}

void displayStatus(const char* text, uint16_t color) {
  if (color == lastStatusColor && strcmp(text, lastStatus) == 0) return;
  tft.fillRect(0, 210, 240, 22, COL_BLACK);
  centerText(text, 213, 2, color);
  strncpy(lastStatus, text, sizeof(lastStatus) - 1);
  lastStatus[sizeof(lastStatus) - 1] = '\0';
  lastStatusColor = color;
}

void displayIdle(const char* status) {
  tft.fillRect(0, 122, 240, 20, COL_BLACK);
  drawScale(0, false, 0);
  displayStatus(status, COL_WHITE);
}

void displayBattery(const char* text, uint16_t color) {
  tft.fillRect(0, 232, 240, 8, COL_BLACK);
  centerText(text, 232, 1, color);
}

void displayMenuOpen() {
  displayClearAll();
  centerText("SETTINGS", 8, 2, COL_YELLOW);
  centerText("MENU: next item   < > : change", 200, 1, COL_GREY);
  centerText("OK: save and exit", 214, 1, COL_GREY);
}

void displayMenuRows(int selected, const char* const labels[], const char* const values[], int n) {
  char buf[32];
  for (int i = 0; i < n; i++) {
    int y = 44 + i * 30;
    tft.fillRect(0, y - 2, 240, 20, COL_BLACK);
    snprintf(buf, sizeof(buf), "%c%-6s %s", i == selected ? '>' : ' ', labels[i], values[i]);
    tft.setTextSize(2);
    tft.setTextColor(i == selected ? COL_YELLOW : COL_WHITE);
    tft.setCursor(0, y);
    tft.print(buf);
  }
}
