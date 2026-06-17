#include "ButtonHandler.h"
#include "StorageManager.h"  // for the LOG macro
#include <Arduino.h>

static int btnPin = -1;
static bool lastReading = HIGH;
static unsigned long lastDebounceTime = 0;
static unsigned long pressStartTime = 0;
static bool longPressHandled = false;

const unsigned long DEBOUNCE_DELAY = 30;
const unsigned long LONG_PRESS_DURATION = 2000; // 2 seconds

void buttonInit(int buttonPin)
{
    btnPin = buttonPin;
    pinMode(btnPin, INPUT_PULLUP);
    LOG("Button initialized.");
}

ButtonEvent checkButtonEvent()
{
    if (btnPin < 0) return NONE;
    bool reading = digitalRead(btnPin);

    if (reading != lastReading) {
        lastDebounceTime = millis();
    }

    if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
        if (reading == LOW) {
            if (pressStartTime == 0) {
                pressStartTime = millis();
                if (pressStartTime == 0) pressStartTime = 1;  // guard against millis() == 0 at rollover
                longPressHandled = false;
            } else if (!longPressHandled && (millis() - pressStartTime >= LONG_PRESS_DURATION)) {
                longPressHandled = true;
                LOG("Button long pressed!");
                return LONG_PRESS;
            }
        } else { // released
            if (pressStartTime > 0 && !longPressHandled) {
                LOG("Button short pressed!");
                pressStartTime = 0;
                return SHORT_PRESS;
            }
            pressStartTime = 0;
        }
    }

    lastReading = reading;
    return NONE;
}
