#pragma once

#include <Arduino.h>
#include "config.h"
#include "esp_adc/adc_cali.h"

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

    // Get battery percentage (0-100) based on voltage
    int getPercentage() const;

    // Check if motor should be disabled due to low battery
    bool isMotorAllowed() const { return status != BatteryStatus::CRITICAL; }

    // Single unsmoothed reading, for stall detection during a feed.
    // getVoltage() is a 20-sample average updated once a second -- far too slow
    // to catch a stall.  This reads the ADC directly and does not disturb the
    // smoothing buffer, so it can be called at will while the motor runs.
    float readInstantVoltage();

    // While the motor runs, the smoothing buffer would fill with loaded readings
    // and then reject resting samples as outliers for several seconds after the
    // feed, corrupting the next stall baseline.  Motor pauses sampling for the
    // duration of a feed.
    void setSamplingPaused(bool paused) { samplingPaused = paused; }

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
    bool samplingPaused = false;
    void refillSamples(uint32_t interSampleDelayMs);

    float readRawVoltage();
    float readSolarVoltage();
    void updateStatus();

    // ADC attenuation is switched between 12 dB and 6 dB depending on pack
    // voltage -- see config.h.  Only the battery pin switches; solar stays at
    // 12 dB because a 6 V panel still reaches ~9.8 V Voc.
    bool using6dB = false;
    void applyAttenuation(bool use6dB);
    void updateAttenuation();

    // Per-attenuation calibration.
    //
    // The Arduino core keeps ONE calibration handle per ADC unit, built for the
    // unit-wide default attenuation (12 dB).  analogSetPinAttenuation() changes a
    // channel's hardware attenuation but does not rebuild that handle, so
    // analogReadMilliVolts() on a 6 dB channel converts with the 12 dB curve and
    // reads ~1.9x high.  Seen on the bench 2026-09-29: a 4.75 V input flipped
    // between 4.75 V and 9.1 V every cycle as the switching chased itself.
    // We hold our own handle for each attenuation and convert raw counts here.
    adc_cali_handle_t cali12dB = nullptr;
    adc_cali_handle_t cali6dB = nullptr;
    bool batteryRawSaturated = false;  // last battery read hit the ADC ceiling
    void initCalibration();
    uint32_t readMilliVolts(uint8_t pin, bool sixDb);
};

extern Battery battery;
