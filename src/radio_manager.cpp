#include "radio_manager.h"
#include "config.h"
#include "ota_server.h"
#include "storage.h"

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

    // Register OTA routes (server only listens while the AP is up)
    otaServer.begin();

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

    // Check for OTA request (app wrote 0x01 to the WiFi OTA characteristic).
    // BLE goes down as part of this transition, so the app must already hold
    // the WiFi credentials before it triggers.
    if (hasWifiOtaRequest()) {
        DEBUG_PRINTLN("RadioManager: WiFi OTA request detected");
        clearWifiOtaRequest();
        if (!isWifiActive()) {
            transitionToWifi();
        } else {
            DEBUG_PRINTLN("RadioManager: WiFi already running, ignoring OTA request");
        }
    }

    // Update active subsystem
    if (currentMode == Mode::WIFI) {
        wifiManager.update();
        otaServer.update();

        // Don't drop the radio out from under a firmware write
        if (otaServer.getState() == OTAServer::State::UPLOADING) {
            return;
        }

        // App cancelled the update: go back to BLE now, not in five minutes.
        // Deferred by a short grace so the HTTP response gets out first.
        if (otaServer.consumeExitRequest()) {
            DEBUG_PRINTLN("RadioManager: OTA exit requested, returning to BLE");
            delay(200);
            transitionToBle();
            return;
        }

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
    otaServer.start();
    DEBUG_PRINTLN("RadioManager: WiFi started (OTA endpoint listening)");
}

void RadioManager::stopWifi() {
    otaServer.stop();
    wifiManager.stop();
    DEBUG_PRINTLN("RadioManager: WiFi stopped");
}

void RadioManager::startBle() {
    // stopBle() deinitialises the whole stack (needed for a clean WiFi
    // handoff), so coming back means re-creating it, not just re-advertising.
    // Without this, every WiFi session that did not end in a reboot left the
    // feeder silent until power-cycled.
    if (!bleManager.isInitialized()) {
        bleManager.begin(storage.getDeviceId());
    }
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

bool RadioManager::hasWifiOtaRequest() {
    return bleManager.hasWifiOtaRequest();
}

void RadioManager::clearWifiOtaRequest() {
    bleManager.clearWifiOtaRequest();
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

bool RadioManager::ensureBleHealthy() {
    // Only check if we're supposed to be in BLE mode
    if (currentMode != Mode::BLE) {
        return false;
    }
    return bleManager.ensureAdvertising();
}
