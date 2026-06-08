#include "PowerLatch.h"
#include "StorageManager.h"  // for the LOG macro
#include <Arduino.h>

static int POWER_LATCH_PIN = -1; // power relay control pin

void initPowerLatch(int powerLatchPin)
{
    POWER_LATCH_PIN = powerLatchPin;
    pinMode(POWER_LATCH_PIN, OUTPUT);
    digitalWrite(POWER_LATCH_PIN, HIGH); // default to powered ON (HIGH)
    delay(1000); // wait 1s to let the supply stabilize
    LOG("Power latch initialized and set to ON.");
}

void powerOn()
{
    if (digitalRead(POWER_LATCH_PIN) == LOW) {
        digitalWrite(POWER_LATCH_PIN, HIGH); // power on
        delay(500); // let the supply stabilize
        LOG("Power ON.");
    } else {
        LOG("Power is already ON.");
    }
}

void powerOff()
{
    if (digitalRead(POWER_LATCH_PIN) == HIGH) {
        digitalWrite(POWER_LATCH_PIN, LOW); // power off
        delay(500); // let the supply settle
        LOG("Power OFF.");
    } else {
        LOG("Power is already OFF.");
    }
}
