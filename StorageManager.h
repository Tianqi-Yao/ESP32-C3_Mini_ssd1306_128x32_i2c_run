#pragma once

#include <Arduino.h>

bool storageInit();
bool saveTemperatureHumidityWithTime(float temp, float hum);
String getTodayLogPath();
bool logMessage(const String& message);

// 加一个全局宏：
#define LOG(msg) do { Serial.println(msg); logMessage(msg); } while(0)
