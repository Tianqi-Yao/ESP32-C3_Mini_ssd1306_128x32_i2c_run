#pragma once
#include <Arduino.h>

// Returns the HTML string showing the sensor/battery status panel.
String sensorUI(float temp, float hum, float batteryVoltage, int batteryPercent, const String& timeStr, bool sdReady);
