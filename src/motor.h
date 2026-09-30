#pragma once

#include <Arduino.h>
#include "config.h"

class Motor {
public:
    void begin();
    void update();

    // Start motor for specified duration (or default)
    void startThrow(uint8_t durationSec = 0);

    // Emergency stop
    void stop();

    // State queries
    bool isRunning() const { return running; }
    uint8_t getRemainingSeconds() const;

    // True if the last feed was cut short by stall detection rather than
    // running to completion.  Cleared when the next feed starts.
    bool wasStalled() const { return stalled; }
    float getLastSagVolts() const { return lastSagVolts; }

    // How long the motor actually ran on the last feed (ms), set when it stops.
    // Differs from the requested duration when a stall or the safety timer cut
    // the feed short.
    uint32_t getLastRunMs() const { return lastRunMs; }

    // Set default duration (stored in settings)
    void setDefaultDuration(uint8_t durationSec);
    uint8_t getDefaultDuration() const { return defaultDuration; }

private:
    bool running = false;
    uint32_t startTime = 0;
    uint32_t runDurationMs = 0;
    uint8_t defaultDuration = MOTOR_DEFAULT_DURATION_SEC;

    // Hardware timer handle for safety cutoff
    hw_timer_t* safetyTimer = nullptr;

    // Stall detection -- see config.h for the rationale and calibration note
    float baselineVoltage = 0.0f;   // pack voltage immediately before the feed
    float lastSagVolts = 0.0f;      // deepest sag seen during the last feed
    uint32_t lastStallCheck = 0;
    uint8_t stallCount = 0;
    bool stalled = false;
    uint32_t lastRunMs = 0;
    uint32_t lastSagTrace = 0;

    void checkForStall();

    void activateMotor();
    void deactivateMotor();

    // Safety timer callback (static for ISR)
    static void IRAM_ATTR onSafetyTimeout();
};

extern Motor motor;
