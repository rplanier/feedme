#include "motor.h"
#include "battery.h"

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
        // The ISR already cut the GPIO; finish what stop() would have done so
        // lastRunMs and the battery sampling state are correct for this feed.
        if (running) {
            lastRunMs = millis() - startTime;
        }
        running = false;
        if (safetyTimer) {
            timerStop(safetyTimer);
        }
        battery.setSamplingPaused(false);
        DEBUG_PRINTLN("Motor: SAFETY TIMEOUT - motor stopped");
    }

    if (!running) {
        return;
    }

    // Calibration trace: the sag curve through the whole feed, inrush included.
    // Baseline is the smoothed resting voltage captured at startThrow().
    if (MOTOR_SAG_TRACE) {
        uint32_t nowMs = millis();
        if (nowMs - lastSagTrace >= MOTOR_SAG_TRACE_MS) {
            lastSagTrace = nowMs;
            float v = battery.readInstantVoltage();
            DEBUG_PRINTF("Motor: sag trace t=%lums V=%.2f sag=%.2f\n",
                         (unsigned long)(nowMs - startTime), v, baselineVoltage - v);
        }
    }

    // Abort early if the pack is sagging like a locked rotor
    checkForStall();
    if (!running) {
        return;  // checkForStall() stopped us
    }

    // Check if duration has elapsed
    uint32_t elapsed = millis() - startTime;
    if (elapsed >= runDurationMs) {
        stop();
        DEBUG_PRINTF("Motor: Normal stop after duration elapsed (peak sag %.2fV)\n", lastSagVolts);
    }
}

void Motor::checkForStall() {
    uint32_t elapsed = millis() - startTime;

    // Ignore the spin-up window (~2 s to settle, see MOTOR_STALL_BLANK_MS in
    // config.h); sampling inside it would trip this every single feed.
    if (elapsed < MOTOR_STALL_BLANK_MS) {
        return;
    }

    uint32_t now = millis();
    if (now - lastStallCheck < MOTOR_STALL_SAMPLE_MS) {
        return;
    }
    lastStallCheck = now;

    float sag = baselineVoltage - battery.readInstantVoltage();
    if (sag > lastSagVolts) {
        lastSagVolts = sag;
    }

    if (sag >= MOTOR_STALL_SAG_VOLTS) {
        // Require consecutive samples: a single deep reading is more likely to
        // be ADC noise or a transient than a genuine jam.
        if (++stallCount >= MOTOR_STALL_CONFIRM) {
            stalled = true;
            stop();
            DEBUG_PRINTF("Motor: STALL DETECTED - sag %.2fV (limit %.2fV), stopped after %lums\n",
                         sag, MOTOR_STALL_SAG_VOLTS, (unsigned long)elapsed);
        }
    } else {
        stallCount = 0;
    }
}

void Motor::startThrow(uint8_t durationSec) {
    if (running) {
        DEBUG_PRINTLN("Motor: Already running, ignoring start request");
        return;
    }

    // Use provided duration or default
    uint8_t duration = (durationSec > 0) ? durationSec : defaultDuration;

    // Clamp to maximum
    if (duration > MOTOR_MAX_DURATION_SEC) {
        duration = MOTOR_MAX_DURATION_SEC;
        DEBUG_PRINTF("Motor: Duration clamped to max %d sec\n", MOTOR_MAX_DURATION_SEC);
    }

    runDurationMs = duration * 1000UL;
    startTime = millis();

    // Snapshot the resting pack voltage before the motor loads it.  getVoltage()
    // is the smoothed value, which is what we want for a baseline -- and it stays
    // a resting value because sampling is paused for the duration of the feed.
    baselineVoltage = battery.getVoltage();
    battery.setSamplingPaused(true);
    lastSagVolts = 0.0f;
    lastStallCheck = millis();
    lastSagTrace = 0;
    stallCount = 0;
    stalled = false;

    // Start safety timer before activating motor
    // ESP32 Arduino 3.x API: timerAlarm(timer, alarm_value_us, autoreload, reload_count)
    if (safetyTimer) {
        timerRestart(safetyTimer);
        timerAlarm(safetyTimer, MOTOR_MAX_DURATION_SEC * 1000000ULL, false, 0);
    }

    activateMotor();
    running = true;

    DEBUG_PRINTF("Motor: Started for %d seconds\n", duration);
}

void Motor::stop() {
    deactivateMotor();
    if (running) {
        lastRunMs = millis() - startTime;
    }
    running = false;
    battery.setSamplingPaused(false);

    // Stop safety timer (ESP32 Arduino 3.x API)
    if (safetyTimer) {
        timerStop(safetyTimer);
    }

    DEBUG_PRINTLN("Motor: Stopped");
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
    DEBUG_PRINTF("Motor: Activating - setting GPIO %d to %s\n", PIN_MOTOR_RELAY, pinState ? "HIGH" : "LOW");
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
