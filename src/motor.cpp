#include "motor.h"
#include <Arduino.h>
#include <esp_timer.h>

static const int MOTOR_PINS[4] = {10, 11, 12, 13};
static const int STEP_US       = 1200;   // time per half-step

// Half-step sequence for a 4-coil unipolar stepper (ULN2003 driver).
static const uint8_t SEQ[8][4] = {
  {1,0,0,0},{1,1,0,0},{0,1,0,0},{0,1,1,0},
  {0,0,1,0},{0,0,1,1},{0,0,0,1},{1,0,0,1}
};

static esp_timer_handle_t timer;
static volatile int32_t   stepsLeft = 0;
static volatile int       dir = 1;
static volatile bool      busy = false;
static volatile uint32_t  stoppedAt = 0;
static int                phase = 0;

static void release() {
  for (int i = 0; i < 4; i++) digitalWrite(MOTOR_PINS[i], LOW);
}

// Called by the timer every STEP_US. Does one step, or stops when done.
// No busy-waiting, so the rest of the program keeps running smoothly.
static void onStep(void*) {
  if (stepsLeft <= 0) {
    esp_timer_stop(timer);
    release();
    stoppedAt = millis();
    busy = false;
    return;
  }
  phase = (phase + dir) & 7;
  for (int i = 0; i < 4; i++) digitalWrite(MOTOR_PINS[i], SEQ[phase][i]);
  stepsLeft = stepsLeft - 1;
}

void motorInit() {
  for (int i = 0; i < 4; i++) pinMode(MOTOR_PINS[i], OUTPUT);
  release();
  esp_timer_create_args_t args = {};
  args.callback = onStep;
  args.name = "motor";
  esp_timer_create(&args, &timer);
}

void motorMove(int steps) {
  if (steps == 0) return;
  if (busy) esp_timer_stop(timer);
  dir = (steps > 0) ? 1 : 7;   // +7 mod 8 = one step backwards
  stepsLeft = abs(steps);
  busy = true;
  esp_timer_start_periodic(timer, STEP_US);
}

void motorStop() {
  stepsLeft = 0;   // the next timer tick stops and releases the coils
}

bool motorBusy() { return busy; }

uint32_t motorStoppedAt() { return stoppedAt; }
