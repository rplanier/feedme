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

    void activateMotor();
    void deactivateMotor();

    // Safety timer callback (static for ISR)
    static void IRAM_ATTR onSafetyTimeout();
};

extern Motor motor;
