#pragma once

#include <Arduino.h>
#include <esp_pm.h>

// =============================================================================
// Light Sleep Manager
// =============================================================================
// Manages ESP32 light sleep with BLE advertising for power optimization.
// Light sleep allows ~1-2mA power consumption while maintaining BLE advertising,
// compared to ~20-40mA when fully awake.
//
// Usage:
// - Call begin() once during setup
// - Call resetActivityTimer() when BLE client connects or sends data
// - Call hasTimedOut() to check if inactivity timeout has elapsed
// - Call enableLightSleepWithBle() to enter power-saving mode
// - Call disableLightSleep() before entering deep sleep
// =============================================================================

class LightSleepManager {
public:
    // Initialize the light sleep manager
    void begin();

    // Enable automatic light sleep (CPU sleeps between BLE events)
    // BLE advertising continues during light sleep
    void enableLightSleepWithBle();

    // Disable light sleep (full CPU performance)
    void disableLightSleep();

    // Reset the activity timer (call on BLE connect/write)
    void resetActivityTimer();

    // Check if inactivity timeout has elapsed
    // timeoutMs: timeout in milliseconds (0 = never times out)
    bool hasTimedOut(uint32_t timeoutMs) const;

    // Get milliseconds since last activity
    uint32_t getTimeSinceLastActivity() const;

    // Check if light sleep is currently enabled
    bool isLightSleepEnabled() const { return lightSleepEnabled; }

private:
    bool initialized = false;
    bool lightSleepEnabled = false;
    uint32_t lastActivityTime = 0;

    // Power management configuration for light sleep
    esp_pm_config_t pmConfig;
};

extern LightSleepManager lightSleepManager;
