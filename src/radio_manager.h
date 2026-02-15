#pragma once

#include <Arduino.h>
#include "ble_manager.h"
#include "wifi_manager.h"
#include "storage.h"

// Radio state management - handles mutual exclusion between WiFi and BLE
// since they share the ESP32 radio hardware
class RadioManager {
public:
    enum class Mode {
        IDLE,   // Neither WiFi nor BLE active
        BLE,    // BLE advertising active
        WIFI    // WiFi AP active (for future OTA)
    };

    void begin(const char* deviceId);
    void update();

    // State transitions - handles proper sequencing and delays
    void transitionToWifi();
    void transitionToBle();
    void transitionToIdle();

    // Check BLE schedule and transition if needed
    void checkBleSchedules();

    // Query current state
    Mode getMode() const { return currentMode; }
    bool isWifiActive() const { return currentMode == Mode::WIFI; }
    bool isBleActive() const { return currentMode == Mode::BLE; }
    const char* getModeName() const {
        switch (currentMode) {
            case Mode::IDLE: return "IDLE";
            case Mode::BLE: return "BLE";
            case Mode::WIFI: return "WIFI";
            default: return "UNKNOWN";
        }
    }

    // BLE health check - verify advertising is running and restart if needed
    bool ensureBleHealthy();

    // Convenience accessors for status display
    bool isWifiClientConnected() const;
    bool isBleClientConnected() const;
    int getWifiClientCount() const;
    uint32_t getWifiRemainingIdleSeconds() const;

    // Check for pending requests
    bool hasBleWakeRequest();
    void clearBleWakeRequest();
    bool shouldWifiAutoStop() const;

    // WiFi credentials for display
    const char* getWifiSSID() const;
    const char* getWifiPassword() const;

    // Antenna control
    void setAntenna(AntennaType type);

private:
    Mode currentMode = Mode::IDLE;
    const char* deviceId = nullptr;
    uint32_t bootTime = 0;  // millis() at boot for grace period tracking

    // Delay constants for radio settling
    static constexpr uint32_t BLE_DEINIT_DELAY_MS = 100;
    static constexpr uint32_t BLE_WAKE_DELAY_MS = 500;

    // Grace period after boot before BLE schedules are enforced
    static constexpr uint32_t BLE_BOOT_GRACE_PERIOD_MS = 5 * 60 * 1000;  // 5 minutes

    // Check if still within boot grace period
    bool isInBootGracePeriod() const;

    void startWifi();
    void stopWifi();
    void startBle();
    void stopBle();
};

extern RadioManager radioManager;
