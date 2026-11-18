#pragma once

struct BatteryInfo {
  float volts;     // battery voltage, V
  float mA;        // current drawn from the battery, mA
  int   percent;   // rough charge estimate from voltage, 0-100
  bool  onBattery; // false if no battery voltage is seen (e.g. USB power only)
};

// Start the INA219 on I2C. Returns false if the sensor is not found.
bool batteryInit();

// Read the sensor. Returns false if the sensor is missing.
bool batteryRead(BatteryInfo& out);
