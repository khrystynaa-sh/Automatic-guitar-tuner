#include "buttons.h"
#include <Arduino.h>

// Same order as the Button enum: MENU, PREV, NEXT, OK.
static const int BTN_PINS[4] = {17, 18, 8, 3};
static const int POLL_MS = 5;
static const int DEBOUNCE_SAMPLES = 6;   // 6 x 5 ms = 30 ms stable

static QueueHandle_t queue;

// Polled in its own task because the main loop is busy with audio
// for ~100 ms at a time and could miss a short press.
static void buttonTask(void*) {
  bool    stable[4];
  uint8_t count[4] = {0};
  for (int i = 0; i < 4; i++) stable[i] = digitalRead(BTN_PINS[i]);
  for (;;) {
    for (int i = 0; i < 4; i++) {
      bool now = digitalRead(BTN_PINS[i]);
      if (now != stable[i]) {
        if (++count[i] >= DEBOUNCE_SAMPLES) {
          stable[i] = now;
          count[i] = 0;
          if (now == LOW) {               // pressed (pulled to GND)
            Button b = (Button)i;
            xQueueSend(queue, &b, 0);
          }
        }
      } else {
        count[i] = 0;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
  }
}

void buttonsInit() {
  for (int i = 0; i < 4; i++) pinMode(BTN_PINS[i], INPUT_PULLUP);
  queue = xQueueCreate(8, sizeof(Button));
  xTaskCreatePinnedToCore(buttonTask, "buttons", 2048, NULL, 2, NULL, 1);
}

Button buttonsGet() {
  Button b;
  if (xQueueReceive(queue, &b, 0) == pdTRUE) return b;
  return BTN_NONE;
}

const char* buttonName(Button b) {
  switch (b) {
    case BTN_MENU: return "MENU";
    case BTN_PREV: return "PREV";
    case BTN_NEXT: return "NEXT";
    case BTN_OK:   return "OK";
    default:       return "NONE";
  }
}
