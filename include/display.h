#pragma once
#include <stdint.h>

// RGB565 colours
constexpr uint16_t COL_BLACK  = 0x0000;
constexpr uint16_t COL_WHITE  = 0xFFFF;
constexpr uint16_t COL_RED    = 0xF800;
constexpr uint16_t COL_GREEN  = 0x07E0;
constexpr uint16_t COL_YELLOW = 0xFFE0;
constexpr uint16_t COL_CYAN   = 0x07FF;
constexpr uint16_t COL_GREY   = 0xC618;

void displayInit();
void displayClearAll();

// Top two lines: instrument + mode, tuning + motor state.
void displayHeader(const char* instrument, const char* tuning, bool manual, bool motorOn);

// Big note name and "String N". Only redraws if it changed.
void displayNote(const char* name, int stringNum);
void displayClearNote();

// "110.2 Hz  +3 c" line plus the needle scale.
void displayReading(float freq, float cents, uint16_t needleColor);

// Bottom status line. Only redraws if the text or colour changed.
void displayStatus(const char* text, uint16_t color);

// Empty scale (no needle) and a status message.
void displayIdle(const char* status);

// Small battery icon at the very bottom: fill level + "81%", or a lightning
// bolt while charging. percent < 0 shows "USB" (no battery).
void displayBattery(int percent, uint16_t color, bool charging);

// Settings menu: call Open once, then Rows whenever something changes.
void displayMenuOpen();
void displayMenuRows(int selected, const char* const labels[], const char* const values[], int n);
