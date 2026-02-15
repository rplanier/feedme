#include "light_sleep_manager.h"
#include "config.h"

LightSleepManager lightSleepManager;

void LightSleepManager::begin() {
    if (initialized) return;

    lastActivityTime = millis();
    lightSleepEnabled = false;
    initialized = true;

    DEBUG_PRINTLN("LightSleepManager: Initialized");
}

void LightSleepManager::enableLightSleepWithBle() {
    if (!initialized) {
        begin();
    }

    if (lightSleepEnabled) {
        return;  // Already enabled
    }

    // Configure power management for automatic light sleep
    // The ESP32 will automatically enter light sleep when idle,
    // while BLE advertising continues using the LP (low-power) core
#if CONFIG_PM_ENABLE
    pmConfig.max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    pmConfig.min_freq_mhz = 10;  // Minimum CPU frequency during light sleep
    pmConfig.light_sleep_enable = true;

    esp_err_t err = esp_pm_configure(&pmConfig);
    if (err == ESP_OK) {
        lightSleepEnabled = true;
        DEBUG_PRINTLN("LightSleepManager: Light sleep with BLE enabled");
    } else {
        DEBUG_PRINTF("LightSleepManager: Failed to enable light sleep: %d\n", err);
    }
#else
    // Power management not enabled - light sleep is a no-op
    // Note: To enable, add CONFIG_PM_ENABLE=y to sdkconfig
    static bool warnedOnce = false;
    if (!warnedOnce) {
        DEBUG_PRINTLN("LightSleepManager: Power management not enabled in build config");
        warnedOnce = true;
    }
#endif
}

void LightSleepManager::disableLightSleep() {
    if (!lightSleepEnabled) {
        return;  // Already disabled
    }

#if CONFIG_PM_ENABLE
    // Disable automatic light sleep by setting light_sleep_enable to false
    pmConfig.max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    pmConfig.min_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;  // Keep at max
    pmConfig.light_sleep_enable = false;

    esp_err_t err = esp_pm_configure(&pmConfig);
    if (err == ESP_OK) {
        lightSleepEnabled = false;
        DEBUG_PRINTLN("LightSleepManager: Light sleep disabled");
    } else {
        DEBUG_PRINTF("LightSleepManager: Failed to disable light sleep: %d\n", err);
    }
#endif
}

void LightSleepManager::resetActivityTimer() {
    lastActivityTime = millis();
    // Note: No debug logging here - this is called frequently and creates spam
}

bool LightSleepManager::hasTimedOut(uint32_t timeoutMs) const {
    // If timeout is 0, never times out
    if (timeoutMs == 0) {
        return false;
    }

    uint32_t elapsed = millis() - lastActivityTime;
    return elapsed >= timeoutMs;
}

uint32_t LightSleepManager::getTimeSinceLastActivity() const {
    return millis() - lastActivityTime;
}
