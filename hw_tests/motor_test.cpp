#include <Arduino.h>

const int MOTOR_PINS[4] = {10, 11, 12, 13};   // IN1..IN4
const int STEPS_PER_REV = 4096;               // напівкроки на 360° (точно ~4076)
const int STEP_DELAY_US = 1200;               // швидкість (менше = швидше, мін. ~900)
const int REVS          = 3;                  // обертів в кожну сторону
const int PAUSE_MS      = 500;                // пауза між обертами

const uint8_t SEQ[8][4] = {
  {1,0,0,0},{1,1,0,0},{0,1,0,0},{0,1,1,0},
  {0,0,1,0},{0,0,1,1},{0,0,0,1},{1,0,0,1}
};
int phase = 0;

void release() { for (int i = 0; i < 4; i++) digitalWrite(MOTOR_PINS[i], LOW); }

void rotate360(int dir) {          // dir = +1 вперед, -1 назад
  for (int s = 0; s < STEPS_PER_REV; s++) {
    phase = (phase + dir + 8) % 8;
    for (int i = 0; i < 4; i++) digitalWrite(MOTOR_PINS[i], SEQ[phase][i]);
    delayMicroseconds(STEP_DELAY_US);
    if (s % 64 == 0) yield();
  }
  release();                       // знімаємо струм, щоб не грівся
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 4; i++) pinMode(MOTOR_PINS[i], OUTPUT);
  release();
  delay(1000);
  Serial.println("Motor test started");
}

void loop() {
  static int cycle = 1;
  Serial.printf("\n=== Cycle %d ===\n", cycle++);

  for (int r = 1; r <= REVS; r++) {
    Serial.printf("Forward  %d/%d\n", r, REVS);
    rotate360(+1);
    delay(PAUSE_MS);
  }
  delay(1000);

  for (int r = 1; r <= REVS; r++) {
    Serial.printf("Backward %d/%d\n", r, REVS);
    rotate360(-1);
    delay(PAUSE_MS);
  }
  delay(1000);
}
