#include "RTC.h"
#include "StorageManager.h" // 👈 引入 LOG 宏
#include <Wire.h>
#include <RTClib.h>

static RTC_DS3231 rtc;

bool rtcInit()
{
    if (!rtc.begin())
    {
        LOG("❌ RTC not found!");
        return false;
    }

    if (rtc.lostPower())
    {
        LOG("⚠️ RTC lost power, setting to compile time.");
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // 使用编译时间初始化
    }

    LOG("✅ RTC initialized.");
    return true;
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
    LOG("⏱️ RTC time updated manually.");
}
