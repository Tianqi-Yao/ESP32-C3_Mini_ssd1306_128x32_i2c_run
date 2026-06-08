#pragma once
#include <Arduino.h>

// 返回网页 HTML 字符串，展示传感器状态
String sensorUI(float temp, float hum, float batteryVoltage, int batteryPercent, const String& timeStr);
