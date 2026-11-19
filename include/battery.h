#pragma once

struct BatteryInfo {
  float volts;     // measured battery voltage (smoothed), V
  float restVolts; // estimated voltage with no load, V
  float mA;        // current drawn from the battery, mA
  int   percent;   // rough charge estimate from restVolts, 0-100
  bool  onBattery; // false if no battery voltage is seen (e.g. USB power only)
  bool  charging;  // charger plugged in (voltage higher than the battery alone can be)
};

// Start the INA219 on I2C. Returns false if the sensor is not found.
bool batteryInit();

// Read the sensor. Returns false if the sensor is missing.
// underLoad: the motor is running (or just stopped). The voltage sags then,
// so those readings don't change the charge estimate.
bool batteryRead(BatteryInfo& out, bool underLoad);
