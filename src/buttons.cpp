#include "buttons.h"

Buttons buttons;

void Buttons::begin() {
    pinMode(PIN_BUTTON_TOP, INPUT_PULLUP);
    pinMode(PIN_BUTTON_BOTTOM, INPUT_PULLUP);

    topButton = {};
    bottomButton = {};
    pendingEvent = ButtonEvent::NONE;
}

void Buttons::update() {
    uint32_t now = millis();

    // Debounce: only update at BUTTON_DEBOUNCE_MS intervals
    if (now - lastUpdateTime < BUTTON_DEBOUNCE_MS) {
        return;
    }
    lastUpdateTime = now;

    // Read current state (buttons are active low)
    bool topPressed = !digitalRead(PIN_BUTTON_TOP);
    bool bottomPressed = !digitalRead(PIN_BUTTON_BOTTOM);

    // Update each button
    updateButton(topButton, topPressed, ButtonEvent::TOP_PRESS, ButtonEvent::TOP_HOLD);
    updateButton(bottomButton, bottomPressed, ButtonEvent::BOTTOM_PRESS, ButtonEvent::BOTTOM_HOLD);
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
