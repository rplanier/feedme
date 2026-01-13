#include "buttons.h"

Buttons buttons;

// Static volatile counters for ISR - detect button releases during blocking operations
#if SINGLE_BUTTON_MODE
volatile uint8_t Buttons::isrPressCount = 0;

void IRAM_ATTR Buttons::buttonISR() {
    // Count button releases (RISING edge = button released with INPUT_PULLUP)
    if (isrPressCount < 10) isrPressCount++;
}
#else
volatile uint8_t Buttons::isrPrevPressCount = 0;
volatile uint8_t Buttons::isrNextPressCount = 0;

void IRAM_ATTR Buttons::prevButtonISR() {
    if (isrPrevPressCount < 10) isrPrevPressCount++;
}

void IRAM_ATTR Buttons::nextButtonISR() {
    if (isrNextPressCount < 10) isrNextPressCount++;
}
#endif

void Buttons::begin() {
#if SINGLE_BUTTON_MODE
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    singleButton = {};
    singleButton.wasPressed = !digitalRead(PIN_BUTTON);
    // ISR disabled - causing false events. Polling is sufficient for now.
    // attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), buttonISR, RISING);
#else
    pinMode(PIN_BUTTON_PREV, INPUT_PULLUP);
    pinMode(PIN_BUTTON_NEXT, INPUT_PULLUP);
    prevButton = {};
    nextButton = {};
    prevButton.wasPressed = !digitalRead(PIN_BUTTON_PREV);
    nextButton.wasPressed = !digitalRead(PIN_BUTTON_NEXT);
    // ISR disabled - causing false events. Polling is sufficient for now.
    // attachInterrupt(digitalPinToInterrupt(PIN_BUTTON_PREV), prevButtonISR, RISING);
    // attachInterrupt(digitalPinToInterrupt(PIN_BUTTON_NEXT), nextButtonISR, RISING);
#endif
}

void Buttons::update() {
    uint32_t now = millis();

    // Check if there was a long gap (e.g., during e-paper refresh)
    bool longGap = (now - lastUpdateTime > 500);

    // Only use ISR-detected button releases if we were blocked (long gap)
    // Otherwise the normal polling logic will handle it correctly
#if SINGLE_BUTTON_MODE
    noInterrupts();
    uint8_t isrCount = isrPressCount;
    isrPressCount = 0;
    interrupts();
    if (longGap && isrCount > 0) {
        // We were blocked - use ISR-captured events
        nextPressCount = min((uint8_t)10, (uint8_t)(nextPressCount + isrCount));
    }
    // If not a long gap, discard ISR counts - polling will handle it
#else
    noInterrupts();
    uint8_t isrPrevCount = isrPrevPressCount;
    uint8_t isrNextCount = isrNextPressCount;
    isrPrevPressCount = 0;
    isrNextPressCount = 0;
    interrupts();
    if (longGap) {
        // We were blocked - use ISR-captured events
        if (isrPrevCount > 0) {
            prevPressCount = min((uint8_t)10, (uint8_t)(prevPressCount + isrPrevCount));
        }
        if (isrNextCount > 0) {
            nextPressCount = min((uint8_t)10, (uint8_t)(nextPressCount + isrNextCount));
        }
    }
    // If not a long gap, discard ISR counts - polling will handle it
#endif

    // Debounce - don't check too frequently
    if (now - lastUpdateTime < BUTTON_DEBOUNCE_MS) {
        return;
    }

    lastUpdateTime = now;

#if SINGLE_BUTTON_MODE
    bool pressed = !digitalRead(PIN_BUTTON);
    if (longGap && pressed && !singleButton.wasPressed) {
        // Button was pressed during the gap - reset timing
        singleButton.pressStartTime = now;
        singleButton.holdTriggered = false;
    }
    // Always call updateButton to properly track holds
    updateButton(singleButton, pressed, ButtonEvent::NEXT_PRESS, ButtonEvent::NEXT_HOLD);
#else
    bool prevPressed = !digitalRead(PIN_BUTTON_PREV);
    bool nextPressed = !digitalRead(PIN_BUTTON_NEXT);
    if (longGap) {
        if (prevPressed && !prevButton.wasPressed) {
            prevButton.pressStartTime = now;
            prevButton.holdTriggered = false;
        }
        if (nextPressed && !nextButton.wasPressed) {
            nextButton.pressStartTime = now;
            nextButton.holdTriggered = false;
        }
    }
    // Always call updateButton to properly track holds
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
        if (!state.holdTriggered &&
            (now - state.pressStartTime >= BUTTON_HOLD_THRESHOLD_MS)) {
            state.holdTriggered = true;
            pendingHoldEvent = holdEvent;
        }
    }
    else if (!currentlyPressed && state.wasPressed) {
        // Button just released
        state.isPressed = false;
        if (!state.holdTriggered) {
            // Was a short press, queue the event
            if (pressEvent == ButtonEvent::NEXT_PRESS) {
                if (nextPressCount < 10) nextPressCount++;
            } else if (pressEvent == ButtonEvent::PREV_PRESS) {
                if (prevPressCount < 10) prevPressCount++;
            }
        }
        state.holdTriggered = false;
    }
    else {
        state.isPressed = false;
    }

    state.wasPressed = currentlyPressed;
}

ButtonEvent Buttons::getEvent() {
    // Hold events take priority
    if (pendingHoldEvent != ButtonEvent::NONE) {
        ButtonEvent event = pendingHoldEvent;
        pendingHoldEvent = ButtonEvent::NONE;
        return event;
    }

    // Then check press counters
    if (nextPressCount > 0) {
        nextPressCount--;
        return ButtonEvent::NEXT_PRESS;
    }
    if (prevPressCount > 0) {
        prevPressCount--;
        return ButtonEvent::PREV_PRESS;
    }

    return ButtonEvent::NONE;
}

bool Buttons::hasEvent() const {
    return nextPressCount > 0 || prevPressCount > 0 || pendingHoldEvent != ButtonEvent::NONE;
}
