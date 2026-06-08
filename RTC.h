// RTC.h
#pragma once

#include <RTClib.h>

// Initialize the DS3231 RTC module, auto-setting the time if uninitialized.
bool rtcInit();

// Whether the RTC is usable and reporting a plausible time (begin succeeded and year in 2024-2099).
// Time-dependent logic should gate on this so a missing/dead RTC degrades safely.
bool isRtcValid();

// Get the current time as a string (e.g. "2025-04-30 14:36:11").
String getRTCTimeString();

// Get the current RTC time object.
DateTime getCurrentDateTime();

// Set the RTC to a specific DateTime (e.g. for manual sync).
void setRTC(const DateTime& dt);
