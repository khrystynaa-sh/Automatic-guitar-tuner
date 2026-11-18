#pragma once

enum Button { BTN_NONE = -1, BTN_MENU = 0, BTN_PREV, BTN_NEXT, BTN_OK };

// Set up the 4 buttons (each wired from its GPIO to GND) and start
// a small background task that debounces them.
void buttonsInit();

// Next button press, or BTN_NONE if nothing was pressed.
Button buttonsGet();

const char* buttonName(Button b);
