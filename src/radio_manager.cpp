#include "radio_manager.h"
#include "config.h"

RadioManager radioManager;

void RadioManager::begin(const char* id) {
    deviceId = id;
    bootTime = millis();

#if defined(TARGET_XIAO_ESP32C6)
    // Initialize RF switch pins (FM8625H)
    // Must be done BEFORE BLE/WiFi initialization
    pinMode(PIN_RF_SW_PWR, OUTPUT);
    digitalWrite(PIN_RF_SW_PWR, LOW);   // Power on the RF switch
    pinMode(PIN_RF_PORT, OUTPUT);

    // Set antenna based on stored settings
    setAntenna(storage.getSettings().antennaType);
#endif

    // Initialize WiFi manager (but don't start WiFi yet)
    wifiManager.begin(deviceId);

    // Initialize BLE manager
    bleManager.begin(deviceId);

    DEBUG_PRINTLN("RadioManager: Initialized");
}

bool RadioManager::isInBootGracePeriod() const {
    return (millis() - bootTime) < BLE_BOOT_GRACE_PERIOD_MS;
}

void RadioManager::update() {
    // Check for BLE wake request (user connected via BLE and sent wake command)
    if (hasBleWakeRequest()) {
        DEBUG_PRINTLN("RadioManager: BLE wake request detected");
        clearBleWakeRequest();
        if (!isWifiActive()) {
            transitionToWifi();
        } else {
            DEBUG_PRINTLN("RadioManager: WiFi already running, ignoring wake request");
        }
    }

    // Update active subsystem
    if (currentMode == Mode::WIFI) {
        wifiManager.update();

        // Check for WiFi idle timeout
        if (shouldWifiAutoStop()) {
            DEBUG_PRINTLN("RadioManager: WiFi idle timeout");
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

    DEBUG_PRINTLN("RadioManager: Transitioning to WiFi");

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

    DEBUG_PRINTLN("RadioManager: Transitioning to BLE");

    // Stop WiFi first if running
    if (currentMode == Mode::WIFI) {
        stopWifi();
    }

    // During boot grace period, always start BLE regardless of schedules
    // This allows initial app connection for configuration
    if (isInBootGracePeriod()) {
        DEBUG_PRINTLN("RadioManager: Boot grace period active - starting BLE");
        startBle();
        currentMode = Mode::BLE;
        return;
    }

    // Check if BLE should be active based on schedules
    if (storage.shouldBleBeActive()) {
        startBle();
        currentMode = Mode::BLE;
    } else {
        currentMode = Mode::IDLE;
        DEBUG_PRINTLN("RadioManager: BLE schedule inactive, staying idle");
    }
}

void RadioManager::transitionToIdle() {
    DEBUG_PRINTLN("RadioManager: Transitioning to IDLE");

    if (currentMode == Mode::WIFI) {
        stopWifi();
    } else if (currentMode == Mode::BLE) {
        stopBle();
    }

    currentMode = Mode::IDLE;
}

void RadioManager::checkBleSchedules() {
    // During boot grace period, keep BLE on regardless of schedules
    if (isInBootGracePeriod()) {
        if (currentMode == Mode::IDLE) {
            DEBUG_PRINTLN("RadioManager: Boot grace period - starting BLE");
            startBle();
            currentMode = Mode::BLE;
        }
        return;  // Don't enforce schedules during grace period
    }

    bool shouldBeActive = storage.shouldBleBeActive();

    // Don't start BLE while WiFi is running
    if (shouldBeActive && currentMode == Mode::IDLE) {
        DEBUG_PRINTLN("RadioManager: BLE schedule active - starting BLE");
        startBle();
        currentMode = Mode::BLE;
    } else if (!shouldBeActive && currentMode == Mode::BLE) {
        DEBUG_PRINTLN("RadioManager: BLE schedule inactive - stopping BLE");
        stopBle();
        currentMode = Mode::IDLE;
    }
}

void RadioManager::startWifi() {
    wifiManager.start();
    // WiFi is now running - ready for future OTA endpoint
    DEBUG_PRINTLN("RadioManager: WiFi started (ready for OTA)");
}

void RadioManager::stopWifi() {
    wifiManager.stop();
    DEBUG_PRINTLN("RadioManager: WiFi stopped");
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

void RadioManager::setAntenna(AntennaType type) {
#if defined(TARGET_XIAO_ESP32C6)
    // FM8625H RF switch control:
    // PIN_RF_PORT HIGH = external rod antenna
    // PIN_RF_PORT LOW = onboard PCB antenna
    if (type == AntennaType::ROD) {
        digitalWrite(PIN_RF_PORT, HIGH);
        DEBUG_PRINTLN("RadioManager: External rod antenna selected");
    } else {
        digitalWrite(PIN_RF_PORT, LOW);
        DEBUG_PRINTLN("RadioManager: Onboard PCB antenna selected");
    }
#else
    (void)type;  // Unused on other targets
#endif
}
