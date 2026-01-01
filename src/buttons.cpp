#include "buttons.h"

Buttons buttons;

void Buttons::begin() {
    // External buttons with internal pull-ups (active low)
#if SINGLE_BUTTON_MODE
    // Single button mode: only one button for both navigation and actions
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    singleButton = {};
#else
    // Dual button mode: separate prev/next buttons
    pinMode(PIN_BUTTON_PREV, INPUT_PULLUP);
    pinMode(PIN_BUTTON_NEXT, INPUT_PULLUP);
    prevButton = {};
    nextButton = {};
#endif
    pendingEvent = ButtonEvent::NONE;
}

void Buttons::update() {
    uint32_t now = millis();

    // Debounce: only update at BUTTON_DEBOUNCE_MS intervals
    if (now - lastUpdateTime < BUTTON_DEBOUNCE_MS) {
        return;
    }
    lastUpdateTime = now;

#if SINGLE_BUTTON_MODE
    // Single button: press = next screen, hold = context action
    bool pressed = !digitalRead(PIN_BUTTON);
    updateButton(singleButton, pressed, ButtonEvent::NEXT_PRESS, ButtonEvent::NEXT_HOLD);
#else
    // Read current state (buttons are active low with pull-up)
    bool prevPressed = !digitalRead(PIN_BUTTON_PREV);
    bool nextPressed = !digitalRead(PIN_BUTTON_NEXT);

    // Update each button
    updateButton(prevButton, prevPressed, ButtonEvent::PREV_PRESS, ButtonEvent::PREV_HOLD);
    updateButton(nextButton, nextPressed, ButtonEvent::NEXT_PRESS, ButtonEvent::NEXT_HOLD);
#endif
}

void Buttons::updateButton(ButtonState& state, bool currentlyPressed,
                           ButtonEvent pressEvent, ButtonEvent holdEvent) {
    uint32_t now = millis();

    if (currentlyPressed && !state.wasPressed) {
        // Button just pressed
        state.isPressed = true;
        state.pressStartTime = now;
        state.holdTriggered = false;
    }
    else if (currentlyPressed && state.wasPressed) {
        // Button still held
        state.isPressed = true;

        // Check for hold threshold
        if (!state.holdTriggered &&
            (now - state.pressStartTime >= BUTTON_HOLD_THRESHOLD_MS)) {
            state.holdTriggered = true;
            pendingEvent = holdEvent;
        }
    }
    else if (!currentlyPressed && state.wasPressed) {
        // Button just released
        state.isPressed = false;

        // If hold wasn't triggered, this is a press event
        if (!state.holdTriggered) {
            pendingEvent = pressEvent;
        }
    }
    else {
        // Button not pressed
        state.isPressed = false;
    }

    state.wasPressed = currentlyPressed;
}

ButtonEvent Buttons::getEvent() {
    ButtonEvent event = pendingEvent;
    pendingEvent = ButtonEvent::NONE;
    return event;
}
