#pragma once

#include <Arduino.h>
#include "config.h"

// Button event types
enum class ButtonEvent {
    NONE,
    TOP_PRESS,      // Previous
    TOP_HOLD,       // Back
    BOTTOM_PRESS,   // Next
    BOTTOM_HOLD     // Select
};

// Button state for internal tracking
struct ButtonState {
    bool isPressed;
    bool wasPressed;
    bool holdTriggered;
    uint32_t pressStartTime;
};

class Buttons {
public:
    void begin();
    void update();
    ButtonEvent getEvent();

    // Direct state access if needed
    bool isTopPressed() const { return topButton.isPressed; }
    bool isBottomPressed() const { return bottomButton.isPressed; }

private:
    ButtonState topButton = {};
    ButtonState bottomButton = {};
    ButtonEvent pendingEvent = ButtonEvent::NONE;
    uint32_t lastUpdateTime = 0;

    void updateButton(ButtonState& state, bool currentlyPressed,
                      ButtonEvent pressEvent, ButtonEvent holdEvent);
};

extern Buttons buttons;
