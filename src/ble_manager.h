#pragma once

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <atomic>

// =============================================================================
// BLE UUIDs for FeedMe GATT Service
// =============================================================================

// Primary service UUID
#define FEEDME_SERVICE_UUID        "f33d0001-1234-5678-9abc-def012345678"

// Characteristic UUIDs (per plan architecture)
#define FEEDME_DEVICE_INFO_UUID    "f33d0010-1234-5678-9abc-def012345678"  // Read: version, deviceId, locked
#define FEEDME_AUTH_UUID           "f33d0011-1234-5678-9abc-def012345678"  // Write/Notify: PIN auth
#define FEEDME_STATUS_UUID         "f33d0012-1234-5678-9abc-def012345678"  // Read/Notify: battery, time
#define FEEDME_SETTINGS_UUID       "f33d0013-1234-5678-9abc-def012345678"  // Read/Write: settings
#define FEEDME_FEED_SCHEDULES_UUID "f33d0014-1234-5678-9abc-def012345678"  // Read/Write: feed schedules
#define FEEDME_FEED_CMD_UUID       "f33d0015-1234-5678-9abc-def012345678"  // Write: trigger feed
#define FEEDME_HISTORY_UUID        "f33d0016-1234-5678-9abc-def012345678"  // Read: feed history
#define FEEDME_TIME_SYNC_UUID      "f33d0017-1234-5678-9abc-def012345678"  // Write: sync time
#define FEEDME_WIFI_OTA_UUID       "f33d0018-1234-5678-9abc-def012345678"  // Write: enable WiFi for OTA
#define FEEDME_SET_PIN_UUID        "f33d0019-1234-5678-9abc-def012345678"  // Write: change PIN
#define FEEDME_BLE_SCHEDULES_UUID  "f33d001a-1234-5678-9abc-def012345678"  // Read/Write: BLE advertising schedules

// =============================================================================
// BLE Session State
// =============================================================================

struct BLESession {
    bool authenticated = false;     // True if PIN validated for this connection
    bool isPairedDevice = false;    // True if device was already in paired list
    uint8_t failedAttempts = 0;     // Failed PIN attempts this session
    uint32_t lockoutUntil = 0;      // Lockout timestamp (millis)
    char generatedPin[8] = "";      // Random PIN generated for pairing (4 digits + null)
    char clientAddress[18] = "";    // BLE address of connected client (XX:XX:XX:XX:XX:XX)

    void reset() {
        authenticated = false;
        isPairedDevice = false;
        failedAttempts = 0;
        lockoutUntil = 0;
        generatedPin[0] = '\0';
        clientAddress[0] = '\0';
    }

    bool isLockedOut() const {
        return lockoutUntil > 0 && millis() < lockoutUntil;
    }

    uint32_t getLockoutRemaining() const {
        if (!isLockedOut()) return 0;
        return (lockoutUntil - millis()) / 1000;
    }

    bool needsPairing() const {
        return !isPairedDevice && !authenticated && generatedPin[0] != '\0';
    }
};

// =============================================================================
// BLE Manager Class
// =============================================================================

class BLEManager {
public:
    void begin(const char* deviceId);
    void update();
    void start();
    void stop();
    void deinit();  // Fully release BLE radio for WiFi

    bool isRunning() const { return running; }
    bool isClientConnected() const { return clientConnected; }
    bool hasWakeRequest() const { return wakeRequested.load(); }
    void clearWakeRequest() { wakeRequested.store(false); }

    // WiFi OTA request (written by iOS app to enable WiFi)
    bool hasWifiOtaRequest() const { return wifiOtaRequested.load(); }
    void clearWifiOtaRequest() { wifiOtaRequested.store(false); }

    // Session management
    BLESession& getSession() { return session; }
    void resetSession() { session.reset(); }

    // Notify clients of status changes
    void notifyStatus();
    void notifyAuth(bool success, uint8_t attemptsRemaining = 5, uint32_t lockoutSeconds = 0);

    // Pre-populate characteristic values (call after auth or data changes)
    void updateCharacteristicValues();

    // Chunked data transfer for large payloads
    void sendChunkedData(BLECharacteristic* pChar, const String& data);

    // Pairing PIN management
    void generatePairingPin();                      // Generate random 4-digit PIN
    const char* getPairingPin() const { return session.generatedPin; }
    bool isPairingInProgress() const { return session.needsPairing(); }
    void clearPairingDisplay();                     // Clear PIN from display after pairing

private:
    bool running = false;
    bool initialized = false;
    bool clientConnected = false;
    std::atomic<bool> wakeRequested{false};
    std::atomic<bool> wifiOtaRequested{false};
    char deviceName[16] = "";  // "FeedMe-XXXX"

    BLESession session;

    BLEServer* pServer = nullptr;

    // Characteristics
    BLECharacteristic* pDeviceInfoChar = nullptr;
    BLECharacteristic* pAuthChar = nullptr;
    BLECharacteristic* pStatusChar = nullptr;
    BLECharacteristic* pSettingsChar = nullptr;
    BLECharacteristic* pFeedSchedulesChar = nullptr;
    BLECharacteristic* pFeedCmdChar = nullptr;
    BLECharacteristic* pHistoryChar = nullptr;
    BLECharacteristic* pTimeSyncChar = nullptr;
    BLECharacteristic* pWifiOtaChar = nullptr;
    BLECharacteristic* pSetPinChar = nullptr;
    BLECharacteristic* pBleSchedulesChar = nullptr;

    // Callback classes need access to private members
    friend class ServerCallbacks;
    friend class DeviceInfoCallbacks;
    friend class AuthCallbacks;
    friend class StatusCallbacks;
    friend class SettingsCallbacks;
    friend class FeedSchedulesCallbacks;
    friend class FeedCmdCallbacks;
    friend class HistoryCallbacks;
    friend class TimeSyncCallbacks;
    friend class WifiOtaCallbacks;
    friend class SetPinCallbacks;
    friend class BleSchedulesCallbacks;
};

// =============================================================================
// Callback Classes
// =============================================================================

// Server connection callbacks
class ServerCallbacks : public BLEServerCallbacks {
public:
    ServerCallbacks(BLEManager* manager) : manager(manager) {}
    void onConnect(BLEServer* pServer) override;
    void onDisconnect(BLEServer* pServer) override;
private:
    BLEManager* manager;
};

// Device Info - always readable (no auth required)
class DeviceInfoCallbacks : public BLECharacteristicCallbacks {
public:
    DeviceInfoCallbacks(BLEManager* manager) : manager(manager) {}
    void onRead(BLECharacteristic* pCharacteristic) override;
private:
    BLEManager* manager;
};

// Auth - write PIN to authenticate, notifies with result
class AuthCallbacks : public BLECharacteristicCallbacks {
public:
    AuthCallbacks(BLEManager* manager) : manager(manager) {}
    void onWrite(BLECharacteristic* pCharacteristic) override;
private:
    BLEManager* manager;
};

// Status - read battery, time, next feed (requires auth)
class StatusCallbacks : public BLECharacteristicCallbacks {
public:
    StatusCallbacks(BLEManager* manager) : manager(manager) {}
    void onRead(BLECharacteristic* pCharacteristic) override;
private:
    BLEManager* manager;
};

// Settings - read/write device settings (requires auth)
class SettingsCallbacks : public BLECharacteristicCallbacks {
public:
    SettingsCallbacks(BLEManager* manager) : manager(manager) {}
    void onRead(BLECharacteristic* pCharacteristic) override;
    void onWrite(BLECharacteristic* pCharacteristic) override;
private:
    BLEManager* manager;
};

// Feed Schedules - read/write feed schedules (requires auth)
class FeedSchedulesCallbacks : public BLECharacteristicCallbacks {
public:
    FeedSchedulesCallbacks(BLEManager* manager) : manager(manager) {}
    void onRead(BLECharacteristic* pCharacteristic) override;
    void onWrite(BLECharacteristic* pCharacteristic) override;
private:
    BLEManager* manager;
};

// Feed Command - write duration to trigger feed (requires auth)
class FeedCmdCallbacks : public BLECharacteristicCallbacks {
public:
    FeedCmdCallbacks(BLEManager* manager) : manager(manager) {}
    void onWrite(BLECharacteristic* pCharacteristic) override;
private:
    BLEManager* manager;
};

// History - read feed event history (requires auth)
class HistoryCallbacks : public BLECharacteristicCallbacks {
public:
    HistoryCallbacks(BLEManager* manager) : manager(manager) {}
    void onRead(BLECharacteristic* pCharacteristic) override;
private:
    BLEManager* manager;
};

// Time Sync - write epoch + offset (requires auth)
class TimeSyncCallbacks : public BLECharacteristicCallbacks {
public:
    TimeSyncCallbacks(BLEManager* manager) : manager(manager) {}
    void onWrite(BLECharacteristic* pCharacteristic) override;
private:
    BLEManager* manager;
};

// WiFi OTA - write 1 to enable WiFi for firmware update (requires auth)
class WifiOtaCallbacks : public BLECharacteristicCallbacks {
public:
    WifiOtaCallbacks(BLEManager* manager) : manager(manager) {}
    void onWrite(BLECharacteristic* pCharacteristic) override;
private:
    BLEManager* manager;
};

// Set PIN - write new PIN (requires auth)
class SetPinCallbacks : public BLECharacteristicCallbacks {
public:
    SetPinCallbacks(BLEManager* manager) : manager(manager) {}
    void onWrite(BLECharacteristic* pCharacteristic) override;
private:
    BLEManager* manager;
};

// BLE Schedules - read/write BLE advertising schedules (requires auth)
class BleSchedulesCallbacks : public BLECharacteristicCallbacks {
public:
    BleSchedulesCallbacks(BLEManager* manager) : manager(manager) {}
    void onRead(BLECharacteristic* pCharacteristic) override;
    void onWrite(BLECharacteristic* pCharacteristic) override;
private:
    BLEManager* manager;
};

extern BLEManager bleManager;
