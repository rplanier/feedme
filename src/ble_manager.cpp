#include "ble_manager.h"

BLEManager bleManager;

// Server callbacks implementation
void ServerCallbacks::onConnect(BLEServer* pServer) {
    manager->clientConnected = true;
    Serial.println("BLE: Client connected");
}

void ServerCallbacks::onDisconnect(BLEServer* pServer) {
    manager->clientConnected = false;
    Serial.println("BLE: Client disconnected");
    // Restart advertising after disconnect (if BLE should still be active)
    if (manager->isRunning()) {
        BLEDevice::startAdvertising();
    }
}

// Wake characteristic callbacks implementation
void WakeCallbacks::onWrite(BLECharacteristic* pCharacteristic) {
    // Any write to this characteristic triggers a wake request
    manager->wakeRequested.store(true);
    Serial.println("BLE wake request received");
}

void BLEManager::begin(const char* deviceId) {
    if (initialized) {
        return;
    }

    // Build device name: "FeedMe-XXXX"
    snprintf(deviceName, sizeof(deviceName), "FeedMe-%s", deviceId);

    Serial.printf("Initializing BLE as '%s'\n", deviceName);

    // Initialize BLE
    BLEDevice::init(deviceName);

    // Set maximum TX power for better range
    BLEDevice::setPower(ESP_PWR_LVL_P9);  // +9 dBm (maximum)

    // Create BLE Server
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks(this));

    // Create BLE Service
    BLEService* pService = pServer->createService(FEEDME_SERVICE_UUID);

    // Create Wake Characteristic (writable)
    pWakeCharacteristic = pService->createCharacteristic(
        FEEDME_WAKE_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    pWakeCharacteristic->setCallbacks(new WakeCallbacks(this));

    // Set initial value
    uint8_t value = 0;
    pWakeCharacteristic->setValue(&value, 1);

    // Start the service
    pService->start();

    initialized = true;
    Serial.println("BLE initialized");
}

void BLEManager::start() {
    if (!initialized) {
        Serial.println("BLE not initialized, cannot start");
        return;
    }

    if (running) {
        return;
    }

    // Start advertising
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(FEEDME_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);  // Helps with iPhone connection issues
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    running = true;
    Serial.println("BLE advertising started");
}

void BLEManager::stop() {
    if (!running) {
        return;
    }

    // Disconnect any connected clients first (required for clean WiFi handoff)
    if (pServer && pServer->getConnectedCount() > 0) {
        pServer->disconnect(pServer->getConnId());
        delay(100);
    }

    BLEDevice::stopAdvertising();
    running = false;
    clientConnected = false;

    Serial.println("BLE: Stopped");
}

void BLEManager::deinit() {
    stop();  // Stop advertising first

    if (initialized) {
        BLEDevice::deinit(false);  // false = don't release memory (faster reinit)
        initialized = false;
        pServer = nullptr;
        pWakeCharacteristic = nullptr;
        Serial.println("BLE: Deinitialized");
    }
}

void BLEManager::update() {
    // Nothing to do in update for now
    // The callbacks handle everything asynchronously
}
