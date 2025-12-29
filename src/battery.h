#pragma once

#include <Arduino.h>
#include "config.h"

enum class BatteryStatus {
    GOOD,
    OKAY,
    LOW_BATTERY,
    CRITICAL
};

class Battery {
public:
    void begin();
    void update();

    // Get current readings
    float getVoltage() const { return voltage; }
    BatteryStatus getStatus() const { return status; }
    const char* getStatusText() const;
    bool isCharging() const { return charging; }

    // Check if motor should be disabled due to low battery
    bool isMotorAllowed() const { return status != BatteryStatus::CRITICAL; }

private:
    float voltage = 0.0f;
    float previousVoltage = 0.0f;
    BatteryStatus status = BatteryStatus::OKAY;
    bool charging = false;

    uint32_t lastReadTime = 0;
    static constexpr uint32_t READ_INTERVAL_MS = 1000;  // Read every second

    // Smoothing
    static constexpr int SAMPLE_COUNT = 10;
    float samples[SAMPLE_COUNT] = {};
    int sampleIndex = 0;
    bool samplesReady = false;

    float readRawVoltage();
    void updateStatus();
};

extern Battery battery;
