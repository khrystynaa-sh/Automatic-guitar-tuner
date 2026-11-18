#include "battery.h"
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_INA219.h>

#define INA_SDA 9
#define INA_SCL 14

static Adafruit_INA219 ina;   // default I2C address 0x40
static bool found = false;

// Rough single-cell LiPo curve (voltage -> %). Under load the voltage
// sags, so the estimate reads a bit low while the motor is running.
static const float CURVE_V[] = {3.30f, 3.50f, 3.60f, 3.65f, 3.70f, 3.75f, 3.80f, 3.90f, 4.00f, 4.10f, 4.20f};
static const int   CURVE_P[] = {0,     4,     10,    18,    28,    38,    48,    63,    78,    90,    100};
static const int   CURVE_N   = sizeof(CURVE_V) / sizeof(CURVE_V[0]);

static int percentFromVolts(float v) {
  if (v <= CURVE_V[0]) return 0;
  if (v >= CURVE_V[CURVE_N - 1]) return 100;
  for (int i = 1; i < CURVE_N; i++) {
    if (v < CURVE_V[i]) {
      float t = (v - CURVE_V[i - 1]) / (CURVE_V[i] - CURVE_V[i - 1]);
      return (int)(CURVE_P[i - 1] + t * (CURVE_P[i] - CURVE_P[i - 1]) + 0.5f);
    }
  }
  return 100;
}

bool batteryInit() {
  Wire.setPins(INA_SDA, INA_SCL);
  found = ina.begin(&Wire);
  if (found) ina.setCalibration_32V_1A();   // up to ~1.3 A, 0.04 mA resolution
  Serial.println(found ? "INA219 found" : "INA219 not found - battery info disabled");
  return found;
}

bool batteryRead(BatteryInfo& out) {
  if (!found) return false;
  float bus   = ina.getBusVoltage_V();          // voltage after the shunt
  float shunt = ina.getShuntVoltage_mV() / 1000.0f;
  out.volts     = bus + shunt;                   // = voltage at the battery
  out.mA        = ina.getCurrent_mA();
  out.onBattery = out.volts > 2.5f;
  out.percent   = percentFromVolts(out.volts);
  return true;
}
