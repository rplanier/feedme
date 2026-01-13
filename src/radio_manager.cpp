#include "radio_manager.h"

RadioManager radioManager;

void RadioManager::begin(const char* id) {
    deviceId = id;

    // Initialize WiFi manager (but don't start WiFi yet)
    wifiManager.begin(deviceId);

    // Initialize BLE manager
    bleManager.begin(deviceId);

    Serial.println("RadioManager: Initialized");
}

void RadioManager::update() {
    // Check for BLE wake request (user connected via BLE and sent wake command)
    if (hasBleWakeRequest()) {
        Serial.println("RadioManager: BLE wake request detected");
        clearBleWakeRequest();
        if (!isWifiActive()) {
            transitionToWifi();
        } else {
            Serial.println("RadioManager: WiFi already running, ignoring wake request");
        }
    }

    // Update active subsystem
    if (currentMode == Mode::WIFI) {
        wifiManager.update();

        // Check for WiFi idle timeout
        if (shouldWifiAutoStop()) {
            Serial.println("RadioManager: WiFi idle timeout");
            transitionToBle();
        }
    } else if (currentMode == Mode::BLE) {
        bleManager.update();
    }
}

void RadioManager::transitionToWifi() {
    if (currentMode == Mode::WIFI) {
        return;  // Already in WiFi mode
    }

    Serial.println("RadioManager: Transitioning to WiFi");

    // Stop BLE first if running (they share the radio)
    if (currentMode == Mode::BLE) {
        stopBle();
        delay(BLE_DEINIT_DELAY_MS);
    }

    startWifi();
    currentMode = Mode::WIFI;
}

void RadioManager::transitionToBle() {
    if (currentMode == Mode::BLE) {
        return;  // Already in BLE mode
    }

    Serial.println("RadioManager: Transitioning to BLE");

    // Stop WiFi first if running
    if (currentMode == Mode::WIFI) {
        stopWifi();
    }

    // Check if BLE should be active based on schedules
    if (storage.shouldBleBeActive()) {
        startBle();
        currentMode = Mode::BLE;
    } else {
        currentMode = Mode::IDLE;
        Serial.println("RadioManager: BLE schedule inactive, staying idle");
    }
}

void RadioManager::transitionToIdle() {
    Serial.println("RadioManager: Transitioning to IDLE");

    if (currentMode == Mode::WIFI) {
        stopWifi();
    } else if (currentMode == Mode::BLE) {
        stopBle();
    }

    currentMode = Mode::IDLE;
}

void RadioManager::checkBleSchedules() {
    bool shouldBeActive = storage.shouldBleBeActive();

    // Don't start BLE while WiFi is running
    if (shouldBeActive && currentMode == Mode::IDLE) {
        Serial.println("RadioManager: BLE schedule active - starting BLE");
        startBle();
        currentMode = Mode::BLE;
    } else if (!shouldBeActive && currentMode == Mode::BLE) {
        Serial.println("RadioManager: BLE schedule inactive - stopping BLE");
        stopBle();
        currentMode = Mode::IDLE;
    }
}

void RadioManager::startWifi() {
    wifiManager.start();

    // Start web server
    if (!webServerActive) {
        webServer.begin();
        if (throwCallback) {
            webServer.setThrowCallback(throwCallback);
        }
        webServerActive = true;
        Serial.println("RadioManager: WebServer started");
    }
}

void RadioManager::stopWifi() {
    if (webServerActive) {
        webServer.stop();
        webServerActive = false;
        Serial.println("RadioManager: WebServer stopped");
    }
    wifiManager.stop();
}

void RadioManager::startBle() {
    bleManager.start();
}

void RadioManager::stopBle() {
    bleManager.deinit();
}

// Convenience accessors
bool RadioManager::isWifiClientConnected() const {
    return wifiManager.getClientCount() > 0;
}

bool RadioManager::isBleClientConnected() const {
    return bleManager.isClientConnected();
}

int RadioManager::getWifiClientCount() const {
    return wifiManager.getClientCount();
}

uint32_t RadioManager::getWifiRemainingIdleSeconds() const {
    return wifiManager.getRemainingIdleSeconds();
}

bool RadioManager::hasBleWakeRequest() {
    return bleManager.hasWakeRequest();
}

void RadioManager::clearBleWakeRequest() {
    bleManager.clearWakeRequest();
}

bool RadioManager::shouldWifiAutoStop() const {
    return wifiManager.shouldAutoStop();
}

const char* RadioManager::getWifiSSID() const {
    return wifiManager.getSSID();
}

const char* RadioManager::getWifiPassword() const {
    return wifiManager.getPassword();
}

void RadioManager::setThrowCallback(ThrowCallback callback) {
    throwCallback = callback;
    // If web server is already active, update its callback too
    if (webServerActive) {
        webServer.setThrowCallback(callback);
    }
}
