#pragma once

#include <Arduino.h>

bool storageInit();
bool saveTemperatureHumidityWithTime(float temp, float hum);
String getTodayLogPath();
bool logMessage(const String& message);

// Global logging macro: print to serial and append to the SD log file.
#define LOG(msg) do { Serial.println(msg); logMessage(msg); } while(0)
