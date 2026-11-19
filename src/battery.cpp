#include "battery.h"
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_INA219.h>

#define INA_SDA 9
#define INA_SCL 14

// On our board the battery is wired to VIN- (current reads negative while
// discharging). Set to false if VIN+ goes to the battery instead.
static const bool  BATTERY_ON_VIN_MINUS = true;

static const float INTERNAL_OHMS = 0.15f;  // rough LiPo internal resistance
static const float SMOOTH        = 0.3f;   // voltage smoothing between reads

// Calibration for OUR battery: voltage when full, with the charger unplugged
// (the "rest" value in the Serial Monitor).
// LP5467100 (used): 3.84 V measured at ~120 mA -> ~3.86 V at rest.
// For a new battery: charge until blue, unplug the charger, read "rest", put it here.
static const float BATTERY_FULL_V  = 3.86f;
static const float BATTERY_EMPTY_V = 3.30f;

// A plugged-in charger pushes the voltage above what the battery alone
// reaches. Above FULL + ON: charger detected; below FULL + OFF: unplugged.
// If the charging icon misbehaves with a new battery, set CHARGER_DETECT to false.
static const bool  CHARGER_DETECT   = true;
static const float CHARGER_ON_V     = 0.10f;
static const float CHARGER_OFF_V    = 0.05f;

static Adafruit_INA219 ina;   // default I2C address 0x40
static bool  found = false;
static float smoothV = 0;       // voltage, only from readings without motor load
static float idleMA = 0;        // current at those readings
static bool  charging = false;
static int   shownPercent = -1; // what we report; only goes down unless charging

// Typical single-cell LiPo curve at rest (voltage -> %), for a new cell
// that charges to 4.20 V. It is stretched to fit BATTERY_FULL_V.
static const float CURVE_V[] = {3.30f, 3.50f, 3.60f, 3.65f, 3.70f, 3.75f, 3.80f, 3.90f, 4.00f, 4.10f, 4.20f};
static const int   CURVE_P[] = {0,     4,     10,    18,    28,    38,    48,    63,    78,    90,    100};
static const int   CURVE_N   = sizeof(CURVE_V) / sizeof(CURVE_V[0]);

static int percentFromVolts(float v) {
  // Map our battery's empty..full range onto the typical 3.30..4.20 V curve.
  v = CURVE_V[0] + (v - BATTERY_EMPTY_V) * (CURVE_V[CURVE_N - 1] - CURVE_V[0]) /
                                           (BATTERY_FULL_V - BATTERY_EMPTY_V);
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

bool batteryRead(BatteryInfo& out, bool underLoad) {
  if (!found) return false;
  float bus   = ina.getBusVoltage_V();               // voltage at VIN-
  float shunt = ina.getShuntVoltage_mV() / 1000.0f;  // VIN+ minus VIN-
  float mA    = ina.getCurrent_mA();

  float v;
  if (BATTERY_ON_VIN_MINUS) { v = bus;         mA = -mA; }
  else                      { v = bus + shunt;           }

  // The motor makes the voltage dip for a while; keep the last calm value.
  if (smoothV == 0)    { smoothV = v; idleMA = mA; }
  else if (!underLoad) { smoothV += SMOOTH * (v - smoothV); idleMA = mA; }

  if (CHARGER_DETECT) {
    if (v > BATTERY_FULL_V + CHARGER_ON_V)             charging = true;   // raw v: react at once
    else if (smoothV < BATTERY_FULL_V + CHARGER_OFF_V) charging = false;
  }

  // The small steady load (board + display) still sags the voltage a bit;
  // adding I*R gives roughly the voltage at rest.
  float rest = smoothV + (idleMA / 1000.0f) * INTERNAL_OHMS;
  int   p    = percentFromVolts(rest);

  // Without a charger a battery can't get fuller, so the shown value only
  // goes down. Any rise is just the voltage recovering after load.
  if (charging || shownPercent < 0 || p < shownPercent) shownPercent = p;

  out.volts     = smoothV;
  out.mA        = mA;
  out.onBattery = smoothV > 2.5f;
  out.charging  = charging;
  out.restVolts = rest;
  out.percent   = shownPercent;
  return true;
}
