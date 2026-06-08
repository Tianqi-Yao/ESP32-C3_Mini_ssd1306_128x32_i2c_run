#include "RTC.h"
#include "StorageManager.h" // for the LOG macro
#include <Wire.h>
#include <RTClib.h>

static RTC_DS3231 rtc;
static bool rtcBeginOk = false;  // whether rtc.begin() succeeded

bool rtcInit()
{
    rtcBeginOk = rtc.begin();
    if (!rtcBeginOk)
    {
        LOG("RTC not found!");
        return false;
    }

    if (rtc.lostPower())
    {
        LOG("RTC lost power, setting to compile time.");
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // initialize with compile time
    }

    LOG("RTC initialized.");
    return true;
}

// Whether the RTC is usable and reporting a plausible time. Everything time-dependent
// (scheduling, day rollover, log filenames/timestamps) should gate on this so that a
// missing/dead RTC degrades safely instead of acting on garbage time.
bool isRtcValid()
{
    if (!rtcBeginOk) return false;
    int year = rtc.now().year();
    return (year >= 2024 && year <= 2099);
}

String getRTCTimeString()
{
    DateTime now = rtc.now();
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    return String(buf);
}

DateTime getCurrentDateTime()
{
    return rtc.now();
}

void setRTC(const DateTime &dt)
{
    rtc.adjust(dt);
    LOG("RTC time updated manually.");
}
