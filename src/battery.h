#pragma once

#include <Arduino.h>
#include "config.h"

enum class BatteryStatus {
    GOOD,
    FAIR,
    POOR,
    CRITICAL
};

class Battery {
public:
    void begin();
    void update();

    // Get current readings
    float getVoltage() const { return voltage; }
    float getSolarVoltage() const { return solarVoltage; }
    BatteryStatus getStatus() const { return status; }
    const char* getStatusText() const;
    bool isCharging() const { return charging; }
    bool is12V() const { return voltage > BATTERY_TYPE_THRESHOLD; }

    // Check if motor should be disabled due to low battery
    bool isMotorAllowed() const { return status != BatteryStatus::CRITICAL; }

private:
    float voltage = 0.0f;
    float previousVoltage = 0.0f;
    float solarVoltage = 0.0f;
    BatteryStatus status = BatteryStatus::FAIR;
    bool charging = false;

    uint32_t lastReadTime = 0;
    static constexpr uint32_t READ_INTERVAL_MS = 1000;  // Read every second

    // Smoothing - use more samples and slower updates for stability
    static constexpr int SAMPLE_COUNT = 20;
    float samples[SAMPLE_COUNT] = {};
    int sampleIndex = 0;
    bool samplesReady = false;

    float readRawVoltage();
    float readSolarVoltage();
    void updateStatus();
};

extern Battery battery;
