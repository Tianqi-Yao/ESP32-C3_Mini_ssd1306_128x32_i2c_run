#pragma once

enum ButtonEvent {
    NONE,
    SHORT_PRESS,
    LONG_PRESS
};

void buttonInit(int buttonPin);
ButtonEvent checkButtonEvent();