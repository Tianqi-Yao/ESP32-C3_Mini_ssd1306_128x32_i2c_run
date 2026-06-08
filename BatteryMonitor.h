#pragma once

float readBatteryVoltage();              // Returns the actual battery voltage (volts)
int getBatteryPercentage(float voltage); // Estimate SOC (0-100%) from an already-read voltage, no extra sampling
int getBatteryPercentage();              // Convenience wrapper: reads the voltage once internally
