#include "motor.h"

Motor motor;

// Static pointer for ISR access
static volatile bool safetyTriggered = false;

void Motor::begin() {
    // Configure motor pin as output, ensure motor is off
    pinMode(PIN_MOTOR_RELAY, OUTPUT);
    deactivateMotor();

    // Initialize hardware safety timer
    // ESP32 Arduino 3.x API: timerBegin(frequency_hz) returns timer handle
    // Using 1MHz frequency (1µs resolution)
    safetyTimer = timerBegin(1000000);
    if (safetyTimer) {
        timerAttachInterrupt(safetyTimer, &Motor::onSafetyTimeout);
        // Don't set alarm yet - will be set when motor starts
    }
}

void Motor::update() {
    // Check if safety timer triggered
    if (safetyTriggered) {
        safetyTriggered = false;
        running = false;
        Serial.println("Motor: SAFETY TIMEOUT - motor stopped");
    }

    if (!running) {
        return;
    }

    // Check if duration has elapsed
    uint32_t elapsed = millis() - startTime;
    if (elapsed >= runDurationMs) {
        stop();
        Serial.println("Motor: Normal stop after duration elapsed");
    }
}

void Motor::startThrow(uint8_t durationSec) {
    if (running) {
        Serial.println("Motor: Already running, ignoring start request");
        return;
    }

    // Use provided duration or default
    uint8_t duration = (durationSec > 0) ? durationSec : defaultDuration;

    // Clamp to maximum
    if (duration > MOTOR_MAX_DURATION_SEC) {
        duration = MOTOR_MAX_DURATION_SEC;
        Serial.printf("Motor: Duration clamped to max %d sec\n", MOTOR_MAX_DURATION_SEC);
    }

    runDurationMs = duration * 1000UL;
    startTime = millis();

    // Start safety timer before activating motor
    // ESP32 Arduino 3.x API: timerAlarm(timer, alarm_value_us, autoreload, reload_count)
    if (safetyTimer) {
        timerRestart(safetyTimer);
        timerAlarm(safetyTimer, MOTOR_MAX_DURATION_SEC * 1000000ULL, false, 0);
    }

    activateMotor();
    running = true;

    Serial.printf("Motor: Started for %d seconds\n", duration);
}

void Motor::stop() {
    deactivateMotor();
    running = false;

    // Stop safety timer (ESP32 Arduino 3.x API)
    if (safetyTimer) {
        timerStop(safetyTimer);
    }

    Serial.println("Motor: Stopped");
}

uint8_t Motor::getRemainingSeconds() const {
    if (!running) {
        return 0;
    }

    uint32_t elapsed = millis() - startTime;
    if (elapsed >= runDurationMs) {
        return 0;
    }

    return (runDurationMs - elapsed) / 1000;
}

void Motor::setDefaultDuration(uint8_t durationSec) {
    if (durationSec > MOTOR_MAX_DURATION_SEC) {
        durationSec = MOTOR_MAX_DURATION_SEC;
    }
    if (durationSec < 1) {
        durationSec = 1;
    }
    defaultDuration = durationSec;
}

void Motor::activateMotor() {
    bool pinState = MOTOR_INVERTED ? LOW : HIGH;
    Serial.printf("Motor: Activating - setting GPIO %d to %s\n", PIN_MOTOR_RELAY, pinState ? "HIGH" : "LOW");
    digitalWrite(PIN_MOTOR_RELAY, pinState);
}

void Motor::deactivateMotor() {
    digitalWrite(PIN_MOTOR_RELAY, MOTOR_INVERTED ? HIGH : LOW);
}

void IRAM_ATTR Motor::onSafetyTimeout() {
    // Emergency cutoff - called from ISR
    // Directly manipulate GPIO for immediate response
    digitalWrite(PIN_MOTOR_RELAY, MOTOR_INVERTED ? HIGH : LOW);
    safetyTriggered = true;
}
