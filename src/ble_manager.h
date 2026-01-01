#pragma once

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <atomic>

// BLE UUIDs for FeedMe service
#define FEEDME_SERVICE_UUID        "f33d0001-1234-5678-9abc-def012345678"
#define FEEDME_WAKE_CHAR_UUID      "f33d0002-1234-5678-9abc-def012345678"

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

private:
    bool running = false;
    bool initialized = false;
    bool clientConnected = false;
    std::atomic<bool> wakeRequested{false};
    char deviceName[16] = "";  // "FeedMe-XXXX"

    BLEServer* pServer = nullptr;
    BLECharacteristic* pWakeCharacteristic = nullptr;

    // Callback classes need access to private members
    friend class ServerCallbacks;
    friend class WakeCallbacks;
};

// Server connection callbacks
class ServerCallbacks : public BLEServerCallbacks {
public:
    ServerCallbacks(BLEManager* manager) : manager(manager) {}
    void onConnect(BLEServer* pServer) override;
    void onDisconnect(BLEServer* pServer) override;
private:
    BLEManager* manager;
};

// Wake characteristic write callback
class WakeCallbacks : public BLECharacteristicCallbacks {
public:
    WakeCallbacks(BLEManager* manager) : manager(manager) {}
    void onWrite(BLECharacteristic* pCharacteristic) override;
private:
    BLEManager* manager;
};

extern BLEManager bleManager;
