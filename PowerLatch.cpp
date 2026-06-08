#include "PowerLatch.h"
#include "StorageManager.h"  // 👈 引入 LOG 宏
#include <Arduino.h>

static int POWER_LATCH_PIN = -1; // 供电继电器引脚

void initPowerLatch(int powerLatchPin)
{
    POWER_LATCH_PIN = powerLatchPin;
    pinMode(POWER_LATCH_PIN, OUTPUT);
    digitalWrite(POWER_LATCH_PIN, HIGH); // 初始状态为供电（HIGH）
    delay(1000); // 延时1秒，确保供电稳定
    LOG("✅ Power latch initialized and set to ON.");
}

void powerOn()
{
    if (digitalRead(POWER_LATCH_PIN) == LOW) {
        digitalWrite(POWER_LATCH_PIN, HIGH); // 供电
        delay(500); // 延时确保供电稳定
        LOG("⚡ Power ON.");
    } else {
        LOG("⚠️ Power is already ON.");
    }
}

void powerOff()
{
    if (digitalRead(POWER_LATCH_PIN) == HIGH) {
        digitalWrite(POWER_LATCH_PIN, LOW); // 断电
        delay(500); // 延时确保断电稳定
        LOG("🛑 Power OFF.");
    } else {
        LOG("⚠️ Power is already OFF.");
    }
}
