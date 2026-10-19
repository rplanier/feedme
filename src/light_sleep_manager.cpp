#include "light_sleep_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <driver/gpio.h>
#include "config.h"

LightSleepManager lightSleepManager;

// The C6 Arduino libs are built with CONFIG_PM_SLP_DISABLE_GPIO, which floats
// every pin during light sleep. Opt these outputs out so they hold their level:
// MOTOR_CTRL would otherwise drop (R12 turns the motor off mid-feed), and the RF
// switch, e-paper and I2C lines would float.
static void keepOutputsDrivenInSleep() {
#if defined(TARGET_XIAO_ESP32C6)
    const uint8_t pins[] = {
        PIN_MOTOR_RELAY, PIN_RF_SW_PWR, PIN_RF_PORT,
        PIN_EPD_CLK, PIN_EPD_DC, PIN_EPD_MOSI, PIN_EPD_CS,
        PIN_RTC_SDA, PIN_RTC_SCL,
    };
    for (uint8_t pin : pins) {
        gpio_sleep_sel_dis(static_cast<gpio_num_t>(pin));
    }
#endif
}

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
    keepOutputsDrivenInSleep();

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

void LightSleepManager::logStats() const {
#if CONFIG_PM_ENABLE && SERIAL_DEBUG
    // esp_pm_dump_locks() writes to a FILE*. stdout is UART0, whose TX is
    // GPIO16 (the button), so capture into memory and print over USB instead.
    char* buf = nullptr;
    size_t len = 0;
    FILE* f = open_memstream(&buf, &len);
    if (!f) return;
    esp_pm_dump_locks(f);
    fclose(f);
    DEBUG_PRINTLN("--- PM stats ---");
    DEBUG_PRINT(buf);
    free(buf);
#endif
}
