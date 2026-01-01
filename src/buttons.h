#pragma once

#include <Arduino.h>
#include "config.h"

// Button event types (semantic naming for navigation)
enum class ButtonEvent {
    NONE,
    PREV_PRESS,     // Previous page / scroll up / decrease
    PREV_HOLD,      // Back / cancel / exit mode
    NEXT_PRESS,     // Next page / scroll down / increase
    NEXT_HOLD       // Select / confirm / enter mode
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
#if SINGLE_BUTTON_MODE
    bool isPrevPressed() const { return singleButton.isPressed; }
    bool isNextPressed() const { return singleButton.isPressed; }
#else
    bool isPrevPressed() const { return prevButton.isPressed; }
    bool isNextPressed() const { return nextButton.isPressed; }
#endif

private:
#if SINGLE_BUTTON_MODE
    ButtonState singleButton = {};
#else
    ButtonState prevButton = {};
    ButtonState nextButton = {};
#endif
    ButtonEvent pendingEvent = ButtonEvent::NONE;
    uint32_t lastUpdateTime = 0;

    void updateButton(ButtonState& state, bool currentlyPressed,
                      ButtonEvent pressEvent, ButtonEvent holdEvent);
};

extern Buttons buttons;
