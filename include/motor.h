#pragma once
#include <stdint.h>

constexpr int STEPS_PER_REV = 4096;  // half-steps per output turn (28BYJ-48)

// Set up the motor pins and the step timer.
void motorInit();

// Start turning by `steps` half-steps (negative = other direction).
// Returns immediately; a hardware timer steps the motor in the background.
void motorMove(int steps);

// Stop the motor as soon as possible.
void motorStop();

// True while the motor is turning.
bool motorBusy();

// millis() when the motor last stopped.
uint32_t motorStoppedAt();
