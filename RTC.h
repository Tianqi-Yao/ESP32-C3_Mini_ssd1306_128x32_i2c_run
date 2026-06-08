// ClockManager.h
#pragma once

#include <RTClib.h>

// 初始化 PCF8523 RTC 模块，自动设置时间（如未初始化）
bool rtcInit();

// 获取当前时间的字符串表示（例如 "2025-04-30 14:36:11"）
String getRTCTimeString();
// 获取当前时间的本地时间字符串表示（例如 "2025-04-30 14:36:11"）
String getLocalTimeFromRTC();

// 获取当前 RTC 时间对象
DateTime getCurrentDateTime();

// 设置 RTC 时间为指定 DateTime（可用于手动同步）
void setRTC(const DateTime& dt);
