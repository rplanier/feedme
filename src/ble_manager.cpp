#include "ble_manager.h"
#include "storage.h"
#include "battery.h"
#include "motor.h"
#include "rtc_manager.h"
#include "display.h"
#include "time_format.h"
#include "radio_manager.h"
#include "light_sleep_manager.h"
#include <ArduinoJson.h>
#include <esp_random.h>

// NimBLE header for Coded PHY (BLE Long Range) support
#include <host/ble_gap.h>

BLEManager bleManager;

// =============================================================================
// Server Callbacks
// =============================================================================

void ServerCallbacks::onConnect(BLEServer* pServer) {
    manager->clientConnected = true;
    manager->resetSession();  // Fresh session on each connection

    // Reset activity timer - keep device awake while client is connected
    lightSleepManager.resetActivityTimer();

    uint16_t connId = pServer->getConnId();
    DEBUG_PRINTF("BLE: Client connected (conn_id: %d)\n", connId);

    // Request Coded PHY (S=8) for this connection - maximum range (~300m)
    // BLE_GAP_LE_PHY_CODED_S8 = 2 specifies S=8 coding (125 kbps, longest range)
    int phyResult = ble_gap_set_prefered_le_phy(
        connId,
        BLE_GAP_LE_PHY_CODED_MASK,   // TX: prefer Coded PHY
        BLE_GAP_LE_PHY_CODED_MASK,   // RX: prefer Coded PHY
        BLE_GAP_LE_PHY_CODED_S8      // S=8 coding for maximum range
    );
    if (phyResult == 0) {
        DEBUG_PRINTLN("BLE: Requested Coded PHY (S=8) for long range");
    } else {
        DEBUG_PRINTF("BLE: PHY update request returned: %d\n", phyResult);
    }
}

void ServerCallbacks::onDisconnect(BLEServer* pServer) {
    manager->clientConnected = false;
    manager->resetSession();  // Clear auth on disconnect
    DEBUG_PRINTLN("BLE: Client disconnected");

    // Restart advertising after disconnect (if BLE should still be active)
    if (manager->isRunning()) {
        DEBUG_PRINTLN("BLE: Restarting advertising after disconnect");
        BLEDevice::startAdvertising();

        // Verify advertising actually started
        BLEAdvertising* pAdv = BLEDevice::getAdvertising();
        if (pAdv && pAdv->isAdvertising()) {
            DEBUG_PRINTLN("BLE: Advertising confirmed running");
        } else {
            DEBUG_PRINTLN("BLE: WARNING - Advertising failed to restart!");
        }
    } else {
        DEBUG_PRINTLN("BLE: Not restarting advertising (running=false)");
    }
}

// =============================================================================
// Device Info Characteristic (Read - no auth required)
// JSON: { version, deviceId, locked, pinSet }
// =============================================================================

void DeviceInfoCallbacks::onRead(BLECharacteristic* pCharacteristic) {
    JsonDocument doc;
    doc["version"] = FEEDME_VERSION;
    doc["deviceId"] = storage.getDeviceId();
    doc["locked"] = !manager->getSession().authenticated;
    // "pinSet" now indicates whether device requires pairing (always true for security)
    // A paired device will auto-authenticate, so this just tells the app to check auth
    doc["pinSet"] = true;

    String json;
    serializeJson(doc, json);
    pCharacteristic->setValue(json.c_str());

    DEBUG_PRINTF("BLE: DeviceInfo read: %s\n", json.c_str());
}

// =============================================================================
// Public Info Characteristic (Read - no auth required)
// JSON: { name, version, id, configured }
// =============================================================================

void PublicInfoCallbacks::onRead(BLECharacteristic* pCharacteristic) {
    Settings& settings = storage.getSettings();
    JsonDocument doc;

    // Return custom name or fallback to "FeedMe-XXXX"
    if (settings.deviceName[0] != '\0') {
        doc["name"] = settings.deviceName;
    } else {
        char fallback[38];
        snprintf(fallback, sizeof(fallback), "FeedMe-%s", storage.getDeviceId());
        doc["name"] = fallback;
    }

    doc["version"] = FEEDME_VERSION;
    doc["id"] = storage.getDeviceId();
    doc["configured"] = storage.getPairedDeviceCount() > 0;

    String json;
    serializeJson(doc, json);
    pCharacteristic->setValue(json.c_str());

    DEBUG_PRINTF("BLE: PublicInfo read: %s\n", json.c_str());
}

// =============================================================================
// Auth Characteristic (Write/Notify - PIN validation)
// Write: { "pin": "1234" }
// Notify: { "success": true/false, "attemptsRemaining": N, "lockoutSeconds": N }
// =============================================================================

void AuthCallbacks::onWrite(BLECharacteristic* pCharacteristic) {
    // Reset activity timer on BLE interaction
    lightSleepManager.resetActivityTimer();

    String value = pCharacteristic->getValue();
    if (value.length() == 0) {
        manager->notifyAuth(false, PIN_MAX_ATTEMPTS - storage.getFailedPinAttempts());
        return;
    }

    // Parse JSON: { "deviceId": "UUID", "pin": "1234" }
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, value);
    if (error) {
        DEBUG_PRINTF("BLE: Auth JSON parse error: %s\n", error.c_str());
        manager->notifyAuth(false, PIN_MAX_ATTEMPTS - storage.getFailedPinAttempts());
        return;
    }

    // Get iOS device UUID (used to track paired devices)
    const char* deviceId = doc["deviceId"] | "";
    const char* pin = doc["pin"] | "";

    // Store device ID in session if provided
    if (strlen(deviceId) > 0 && strlen(manager->getSession().clientAddress) == 0) {
        strlcpy(manager->getSession().clientAddress, deviceId, sizeof(manager->getSession().clientAddress));
    }

    // Check lockout
    uint32_t lockoutEnd = storage.getLockoutEndTime();
    if (lockoutEnd > 0 && millis() < lockoutEnd) {
        uint32_t remaining = (lockoutEnd - millis()) / 1000;
        manager->notifyAuth(false, 0, remaining);
        DEBUG_PRINTF("BLE: Auth blocked - locked out for %d seconds\n", remaining);
        return;
    }

    // Check for unpair command
    if (doc["unpair"] | false) {
        if (strlen(manager->getSession().clientAddress) > 0) {
            storage.removePairedDevice(manager->getSession().clientAddress);
            DEBUG_PRINTF("BLE: Device unpaired: %s\n", manager->getSession().clientAddress);
        }
        manager->getSession().authenticated = false;
        manager->getSession().isPairedDevice = false;
        manager->notifyAuth(false, PIN_MAX_ATTEMPTS);
        return;
    }

    // Check if this device is already paired (by its UUID)
    if (strlen(manager->getSession().clientAddress) > 0 &&
        storage.isPairedDevice(manager->getSession().clientAddress)) {
        // Device is paired - auto-authenticate
        manager->getSession().authenticated = true;
        manager->getSession().isPairedDevice = true;
        manager->updateCharacteristicValues();  // Pre-populate data for reads
        manager->notifyAuth(true);
        manager->clearPairingDisplay();
        DEBUG_PRINTF("BLE: Auth success (paired device: %s)\n", manager->getSession().clientAddress);
        return;
    }

    // Device not paired - need to verify PIN
    // If no PIN generated yet, generate one and show on display
    if (strlen(manager->getSession().generatedPin) == 0) {
        manager->generatePairingPin();
        // Notify with special response indicating PIN is displayed on device
        manager->notifyAuth(false, PIN_MAX_ATTEMPTS, 0);
        DEBUG_PRINTF("BLE: Pairing PIN generated and displayed: %s\n", manager->getSession().generatedPin);
        return;
    }

    // PIN was already generated, verify the submitted PIN
    if (strlen(pin) == 0) {
        // No PIN submitted, just checking pairing status - PIN is on display
        manager->notifyAuth(false, PIN_MAX_ATTEMPTS - storage.getFailedPinAttempts());
        return;
    }

    // Verify PIN against the generated PIN shown on device
    if (strcmp(pin, manager->getSession().generatedPin) == 0) {
        // PIN matches - pair the device and authenticate
        manager->getSession().authenticated = true;
        manager->getSession().isPairedDevice = true;

        // Add to paired devices list using the iOS device UUID
        if (strlen(manager->getSession().clientAddress) > 0) {
            storage.addPairedDevice(manager->getSession().clientAddress);
        }

        storage.resetFailedPinAttempts();
        manager->updateCharacteristicValues();  // Pre-populate data for reads
        manager->clearPairingDisplay();
        manager->notifyAuth(true);
        DEBUG_PRINTLN("BLE: Auth success (new device paired)");
    } else {
        // Wrong PIN
        storage.incrementFailedPinAttempts();
        uint8_t remaining = PIN_MAX_ATTEMPTS - storage.getFailedPinAttempts();
        uint32_t lockoutSec = 0;

        // Check if we just triggered a lockout
        lockoutEnd = storage.getLockoutEndTime();
        if (lockoutEnd > 0 && millis() < lockoutEnd) {
            lockoutSec = (lockoutEnd - millis()) / 1000;
            remaining = 0;
            // Clear the PIN on lockout so a new one is generated after lockout
            manager->getSession().generatedPin[0] = '\0';
            manager->clearPairingDisplay();
        }

        manager->notifyAuth(false, remaining, lockoutSec);
        DEBUG_PRINTF("BLE: Auth failed (wrong PIN), %d attempts remaining\n", remaining);
    }
}

// =============================================================================
// Status Characteristic (Read/Notify - requires auth)
// JSON: { battery, voltage, charging, time, timeValid, nextFeed, vacationMode }
// =============================================================================

void StatusCallbacks::onRead(BLECharacteristic* pCharacteristic) {
    // Check auth
    if (storage.isPinSet() && !manager->getSession().authenticated) {
        pCharacteristic->setValue("{\"error\":\"unauthorized\"}");
        DEBUG_PRINTLN("BLE: Status read denied - not authenticated");
        return;
    }

    JsonDocument doc;

    // Battery info
    doc["voltage"] = battery.getVoltage();
    doc["charging"] = battery.isCharging();
    doc["battery"] = battery.getPercentage();

    // Time info
    DateTime now = rtcManager.now();
    char timeStr[25];
    snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    doc["time"] = timeStr;
    doc["timeValid"] = rtcManager.isTimeSynced();

    // Next feed
    int hour, minute, daysAway;
    if (storage.getNextRunTime(hour, minute, daysAway)) {
        // getNextRunTime already returns local time (schedules are stored in local time,
        // and SunCalc returns local sunrise/sunset times), so no timezone conversion needed
        char nextFeed[32];
        formatNextFeedDisplay(nextFeed, sizeof(nextFeed), hour, minute, daysAway, 0);
        doc["nextFeed"] = nextFeed;
    } else {
        doc["nextFeed"] = nullptr;
    }

    doc["vacationMode"] = storage.getSettings().vacationMode;

    String json;
    serializeJson(doc, json);
    pCharacteristic->setValue(json.c_str());

    DEBUG_PRINTF("BLE: Status read: %s\n", json.c_str());
}

// =============================================================================
// Settings Characteristic (Read/Write - requires auth)
// JSON: { motorDuration, vacationMode, latitude, longitude, locationSet }
// =============================================================================

void SettingsCallbacks::onRead(BLECharacteristic* pCharacteristic) {
    if (storage.isPinSet() && !manager->getSession().authenticated) {
        pCharacteristic->setValue("{\"error\":\"unauthorized\"}");
        return;
    }

    Settings& settings = storage.getSettings();
    JsonDocument doc;
    doc["deviceName"] = settings.deviceName;
    doc["motorDuration"] = settings.motorDuration;
    doc["vacationMode"] = settings.vacationMode;
    doc["batteryType"] = static_cast<uint8_t>(settings.batteryType);
    doc["antennaType"] = static_cast<uint8_t>(settings.antennaType);
    doc["latitude"] = settings.latitude;
    doc["longitude"] = settings.longitude;
    doc["locationSet"] = settings.locationSet;
    doc["inactivityTimeoutMin"] = settings.inactivityTimeoutMin;

    String json;
    serializeJson(doc, json);

    DEBUG_PRINTF("BLE: Settings read request: %s\n", json.c_str());

    // Send via chunked notifications (for consistency, even though settings are small)
    manager->sendChunkedData(pCharacteristic, json);
}

void SettingsCallbacks::onWrite(BLECharacteristic* pCharacteristic) {
    // Reset activity timer on BLE interaction
    lightSleepManager.resetActivityTimer();

    if (storage.isPinSet() && !manager->getSession().authenticated) {
        DEBUG_PRINTLN("BLE: Settings write denied - not authenticated");
        return;
    }

    String value = pCharacteristic->getValue();
    if (value.length() == 0) return;

    // Handle chunked writes from iOS
    String assembledData;
    if (!manager->receiveChunkedWrite(pCharacteristic->getUUID(), value, assembledData)) {
        // More chunks expected, wait for them
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, assembledData);
    if (error) {
        DEBUG_PRINTF("BLE: Settings JSON parse error: %s\n", error.c_str());
        return;
    }

    Settings& settings = storage.getSettings();
    bool changed = false;

    if (doc["deviceName"].is<const char*>()) {
        const char* name = doc["deviceName"];
        if (strlen(name) < sizeof(settings.deviceName)) {
            strncpy(settings.deviceName, name, sizeof(settings.deviceName) - 1);
            settings.deviceName[sizeof(settings.deviceName) - 1] = '\0';
            changed = true;
            DEBUG_PRINTF("BLE: Device name set to '%s'\n", settings.deviceName);
        }
    }

    if (doc["motorDuration"].is<int>()) {
        uint8_t dur = doc["motorDuration"];
        if (dur >= 1 && dur <= MOTOR_MAX_DURATION_SEC) {
            settings.motorDuration = dur;
            motor.setDefaultDuration(dur);
            changed = true;
        }
    }

    if (doc["vacationMode"].is<bool>()) {
        settings.vacationMode = doc["vacationMode"];
        changed = true;
    }

    if (doc["batteryType"].is<int>()) {
        uint8_t bt = doc["batteryType"];
        if (bt <= 2) {  // Valid range: 0=SLA, 1=AGM, 2=GEL
            settings.batteryType = static_cast<BatteryType>(bt);
            changed = true;
        }
    }

    if (doc["antennaType"].is<int>()) {
        uint8_t at = doc["antennaType"];
        if (at <= 1) {  // Valid range: 0=ROD, 1=ONBOARD
            settings.antennaType = static_cast<AntennaType>(at);
            radioManager.setAntenna(settings.antennaType);
            changed = true;
        }
    }

    if (doc["latitude"].is<float>() && doc["longitude"].is<float>()) {
        settings.latitude = doc["latitude"];
        settings.longitude = doc["longitude"];
        settings.locationSet = true;
        changed = true;
    }

    if (doc["inactivityTimeoutMin"].is<int>()) {
        uint8_t timeout = doc["inactivityTimeoutMin"];
        // Valid range: 0 (never) or 3-60 minutes
        if (timeout == INACTIVITY_NEVER ||
            (timeout >= INACTIVITY_MIN_MINUTES && timeout <= INACTIVITY_MAX_MINUTES)) {
            settings.inactivityTimeoutMin = timeout;
            changed = true;
            DEBUG_PRINTF("BLE: Inactivity timeout set to %d min\n", timeout);
        }
    }

    if (changed) {
        storage.saveSettings();
        DEBUG_PRINTLN("BLE: Settings updated");
    }
}

// =============================================================================
// Feed Schedules Characteristic (Read/Write - requires auth)
// JSON array of feed schedule objects
// =============================================================================

void FeedSchedulesCallbacks::onRead(BLECharacteristic* pCharacteristic) {
    if (storage.isPinSet() && !manager->getSession().authenticated) {
        pCharacteristic->setValue("{\"error\":\"unauthorized\"}");
        return;
    }

    // Build compact JSON for BLE (smaller than full API response)
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (int i = 0; i < storage.getScheduleCount(); i++) {
        Schedule* s = storage.getSchedule(i);
        if (!s) continue;

        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = s->id;
        obj["name"] = s->name;
        obj["hour"] = s->hour;
        obj["minute"] = s->minute;
        obj["days"] = s->days;
        obj["enabled"] = s->enabled;
        obj["scheduleType"] = static_cast<uint8_t>(s->scheduleType);
        obj["sunOffset"] = s->sunOffset;
        obj["duration"] = s->duration;
        obj["startMonth"] = s->startMonth;
        obj["startDay"] = s->startDay;
        obj["endMonth"] = s->endMonth;
        obj["endDay"] = s->endDay;
    }

    String json;
    serializeJson(doc, json);

    DEBUG_PRINTF("BLE: Feed Schedules read request (%d bytes)\n", json.length());

    // Send via chunked notifications (handles large payloads)
    manager->sendChunkedData(pCharacteristic, json);
}

void FeedSchedulesCallbacks::onWrite(BLECharacteristic* pCharacteristic) {
    // Reset activity timer on BLE interaction
    lightSleepManager.resetActivityTimer();

    if (storage.isPinSet() && !manager->getSession().authenticated) {
        DEBUG_PRINTLN("BLE: Feed Schedules write denied - not authenticated");
        return;
    }

    String value = pCharacteristic->getValue();
    if (value.length() == 0) return;

    // Handle chunked writes from iOS (large schedule arrays)
    String assembledData;
    if (!manager->receiveChunkedWrite(pCharacteristic->getUUID(), value, assembledData)) {
        // More chunks expected, wait for them
        return;
    }

    if (storage.setSchedulesFromJson(assembledData)) {
        DEBUG_PRINTLN("BLE: Feed Schedules updated");
        manager->updateCharacteristicValues();  // Refresh cached values
    } else {
        DEBUG_PRINTLN("BLE: Failed to parse feed schedules JSON");
    }
}

// =============================================================================
// Feed Command Characteristic (Write - requires auth)
// Write single byte: duration in seconds (0 = use default)
// =============================================================================

void FeedCmdCallbacks::onWrite(BLECharacteristic* pCharacteristic) {
    // Reset activity timer on BLE interaction
    lightSleepManager.resetActivityTimer();

    DEBUG_PRINTLN("BLE: Feed command received");

    if (storage.isPinSet() && !manager->getSession().authenticated) {
        DEBUG_PRINTLN("BLE: Feed command denied - not authenticated");
        return;
    }

    String value = pCharacteristic->getValue();
    DEBUG_PRINTF("BLE: Feed command value: %s (len=%d)\n", value.c_str(), value.length());
    uint8_t duration = 0;

    if (value.length() > 0) {
        // Could be a raw byte or JSON
        if (value.charAt(0) == '{') {
            JsonDocument doc;
            if (!deserializeJson(doc, value)) {
                // Check for factory reset command
                if (doc["factoryReset"] | false) {
                    DEBUG_PRINTLN("BLE: Factory reset command received");
                    storage.resetToDefaults();
                    storage.clearAllPairedDevices();
                    rtcManager.setTimeSynced(false);
                    DEBUG_PRINTLN("BLE: Factory reset complete - rebooting in 500ms");
                    delay(500);  // Give time for BLE response
                    ESP.restart();
                    return;
                }
                duration = doc["duration"] | 0;
            }
        } else {
            duration = (uint8_t)value.charAt(0);
        }
    }

    // Validate duration
    if (duration == 0) {
        duration = storage.getSettings().motorDuration;
    }
    if (duration > MOTOR_MAX_DURATION_SEC) {
        duration = MOTOR_MAX_DURATION_SEC;
    }

    // Manual BLE feed - user is explicitly requesting, no battery check
    // (matches physical button behavior)

    // Check if motor already running
    if (motor.isRunning()) {
        DEBUG_PRINTLN("BLE: Feed command denied - motor already running");
        return;
    }

    // Start feed
    motor.startThrow(duration);
    storage.logFeedEvent(duration, true, "BLE");
    DEBUG_PRINTF("BLE: Feed started for %d seconds\n", duration);
}

// =============================================================================
// History Characteristic (Read - requires auth)
// JSON array of feed events
// =============================================================================

void HistoryCallbacks::onRead(BLECharacteristic* pCharacteristic) {
    if (storage.isPinSet() && !manager->getSession().authenticated) {
        pCharacteristic->setValue("{\"error\":\"unauthorized\"}");
        return;
    }

    String json = storage.getFeedHistoryJson();

    DEBUG_PRINTF("BLE: History read request (%d bytes)\n", json.length());

    // Send via chunked notifications (handles large payloads)
    manager->sendChunkedData(pCharacteristic, json);
}

// =============================================================================
// Time Sync Characteristic (Write - requires auth)
// JSON: { "epoch": 1705936200, "tzOffset": -360, "posixTz": "CST6CDT,M3.2.0,M11.1.0" }
// epoch: Unix timestamp (UTC)
// tzOffset: minutes from UTC (deprecated, for backwards compatibility)
// posixTz: POSIX timezone string for automatic DST handling
// =============================================================================

void TimeSyncCallbacks::onWrite(BLECharacteristic* pCharacteristic) {
    // Reset activity timer on BLE interaction
    lightSleepManager.resetActivityTimer();

    if (storage.isPinSet() && !manager->getSession().authenticated) {
        DEBUG_PRINTLN("BLE: Time sync denied - not authenticated");
        return;
    }

    String value = pCharacteristic->getValue();
    if (value.length() == 0) return;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, value);
    if (error) {
        DEBUG_PRINTF("BLE: Time sync JSON parse error: %s\n", error.c_str());
        return;
    }

    uint32_t epoch = doc["epoch"] | 0;
    int16_t tzOffset = doc["tzOffset"] | 0;
    const char* posixTz = doc["posixTz"] | "";

    if (epoch > 0) {
        rtcManager.setTime(epoch);
        rtcManager.setTimeSynced(true);

        // Store timezone info
        Settings& settings = storage.getSettings();
        settings.timezoneOffset = tzOffset;  // Keep for backwards compatibility

        // Store POSIX TZ string if provided
        if (posixTz[0] != '\0') {
            strncpy(settings.posixTz, posixTz, sizeof(settings.posixTz) - 1);
            settings.posixTz[sizeof(settings.posixTz) - 1] = '\0';
            DEBUG_PRINTF("BLE: Time synced with POSIX TZ: %s\n", settings.posixTz);
        }

        storage.saveSettings();
        storage.applyTimezone();  // Apply timezone to system

        DEBUG_PRINTF("BLE: Time synced to %u (tz offset: %d)\n", epoch, tzOffset);
    }
}

// =============================================================================
// WiFi OTA Characteristic (Write - requires auth)
// Write 1 to enable WiFi for OTA update
// =============================================================================

void WifiOtaCallbacks::onWrite(BLECharacteristic* pCharacteristic) {
    if (storage.isPinSet() && !manager->getSession().authenticated) {
        DEBUG_PRINTLN("BLE: WiFi OTA denied - not authenticated");
        return;
    }

    String value = pCharacteristic->getValue();
    if (value.length() == 0) return;

    uint8_t cmd = (uint8_t)value.charAt(0);
    if (cmd == 1) {
        manager->wifiOtaRequested.store(true);
        DEBUG_PRINTLN("BLE: WiFi OTA requested");
    }
}

// =============================================================================
// Set PIN Characteristic (Write - requires auth if PIN already set)
// JSON: { "newPin": "5678" }  or  { "newPin": "" } to clear
// =============================================================================

void SetPinCallbacks::onWrite(BLECharacteristic* pCharacteristic) {
    // If PIN is already set, must be authenticated to change it
    if (storage.isPinSet() && !manager->getSession().authenticated) {
        DEBUG_PRINTLN("BLE: Set PIN denied - not authenticated");
        return;
    }

    String value = pCharacteristic->getValue();
    if (value.length() == 0) return;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, value);
    if (error) {
        DEBUG_PRINTF("BLE: Set PIN JSON parse error: %s\n", error.c_str());
        return;
    }

    const char* newPin = doc["newPin"] | "";

    if (strlen(newPin) == 0) {
        // Clear PIN
        storage.clearPin();
        DEBUG_PRINTLN("BLE: PIN cleared");
    } else {
        // Set new PIN
        if (storage.setPin(newPin)) {
            DEBUG_PRINTLN("BLE: PIN updated");
        } else {
            DEBUG_PRINTLN("BLE: Failed to set PIN (invalid format)");
        }
    }
}

// =============================================================================
// BLE Schedules Characteristic (Read/Write - requires auth)
// JSON array of BLE advertising schedule objects
// =============================================================================

void BleSchedulesCallbacks::onRead(BLECharacteristic* pCharacteristic) {
    if (storage.isPinSet() && !manager->getSession().authenticated) {
        pCharacteristic->setValue("{\"error\":\"unauthorized\"}");
        return;
    }

    // Build compact JSON for BLE
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (int i = 0; i < storage.getBleScheduleCount(); i++) {
        BleSchedule* s = storage.getBleSchedule(i);
        if (!s) continue;

        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = s->id;
        obj["startHour"] = s->startHour;
        obj["startMinute"] = s->startMinute;
        obj["endHour"] = s->endHour;
        obj["endMinute"] = s->endMinute;
        obj["days"] = s->days;
        obj["enabled"] = s->enabled;
    }

    String json;
    serializeJson(doc, json);

    DEBUG_PRINTF("BLE: BLE Schedules read request (%d bytes)\n", json.length());

    // Send via chunked notifications (handles large payloads)
    manager->sendChunkedData(pCharacteristic, json);
}

void BleSchedulesCallbacks::onWrite(BLECharacteristic* pCharacteristic) {
    // Reset activity timer on BLE interaction
    lightSleepManager.resetActivityTimer();

    if (storage.isPinSet() && !manager->getSession().authenticated) {
        DEBUG_PRINTLN("BLE: BLE Schedules write denied - not authenticated");
        return;
    }

    String value = pCharacteristic->getValue();
    if (value.length() == 0) return;

    // Handle chunked writes from iOS (large schedule arrays)
    String assembledData;
    if (!manager->receiveChunkedWrite(pCharacteristic->getUUID(), value, assembledData)) {
        // More chunks expected, wait for them
        return;
    }

    // Parse JSON array and update BLE schedules
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, assembledData);
    if (error) {
        DEBUG_PRINTF("BLE: BLE Schedules JSON parse error: %s\n", error.c_str());
        return;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (!arr) {
        DEBUG_PRINTLN("BLE: BLE Schedules - expected JSON array");
        return;
    }

    // Clear existing and rebuild from array
    // First, delete all existing BLE schedules
    while (storage.getBleScheduleCount() > 0) {
        BleSchedule* s = storage.getBleSchedule(0);
        if (s) storage.deleteBleSchedule(s->id);
    }

    // Add schedules from JSON
    for (JsonObject obj : arr) {
        BleSchedule schedule = {};
        schedule.id = obj["id"] | 0;
        strlcpy(schedule.name, obj["name"] | "", sizeof(schedule.name));
        schedule.startHour = obj["startHour"] | 0;
        schedule.startMinute = obj["startMinute"] | 0;
        schedule.endHour = obj["endHour"] | 23;
        schedule.endMinute = obj["endMinute"] | 59;
        schedule.days = obj["days"] | DAYS_ALL;
        schedule.enabled = obj["enabled"] | true;

        storage.addBleSchedule(schedule);
    }

    DEBUG_PRINTF("BLE: BLE Schedules updated (%d schedules)\n", storage.getBleScheduleCount());
    manager->updateCharacteristicValues();  // Refresh cached values
}

// =============================================================================
// BLEManager Implementation
// =============================================================================

void BLEManager::begin(const char* deviceId) {
    if (initialized) {
        return;
    }

    // Use custom device name from settings if set, otherwise default to "FeedMe-XXXX"
    Settings& settings = storage.getSettings();
    if (settings.deviceName[0] != '\0') {
        strncpy(deviceName, settings.deviceName, sizeof(deviceName) - 1);
        deviceName[sizeof(deviceName) - 1] = '\0';
    } else {
        snprintf(deviceName, sizeof(deviceName), "FeedMe-%s", deviceId);
    }

    DEBUG_PRINTF("Initializing BLE as '%s'\n", deviceName);

    // Initialize BLE
    BLEDevice::init(deviceName);

    // Set maximum TX power for better range
    BLEDevice::setPower(ESP_PWR_LVL_P9);  // +9 dBm (maximum)

    // Enable Coded PHY (BLE Long Range) for ~300m range vs ~50m with standard 1M PHY
    // S=8 coding: 125 kbps data rate, maximum range
    // This sets the default PHY preference for all future connections
    int phyResult = ble_gap_set_prefered_default_le_phy(
        BLE_GAP_LE_PHY_CODED_MASK,  // TX: prefer Coded PHY
        BLE_GAP_LE_PHY_CODED_MASK   // RX: prefer Coded PHY
    );
    if (phyResult == 0) {
        DEBUG_PRINTLN("BLE: Coded PHY (Long Range) enabled as default");
    } else {
        DEBUG_PRINTF("BLE: Failed to set Coded PHY default: %d\n", phyResult);
    }

    // Request larger MTU for big JSON payloads (schedules, history)
    BLEDevice::setMTU(517);  // Max is 517 (512 data + 5 overhead)

    // Create BLE Server
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks(this));

    // Create BLE Service
    BLEService* pService = pServer->createService(BLEUUID(FEEDME_SERVICE_UUID), 40);  // 40 handles for all chars

    // -------------------------------------------------------------------------
    // Create all characteristics
    // -------------------------------------------------------------------------

    // Device Info (Read) - always accessible
    pDeviceInfoChar = pService->createCharacteristic(
        FEEDME_DEVICE_INFO_UUID,
        BLECharacteristic::PROPERTY_READ
    );
    pDeviceInfoChar->setCallbacks(new DeviceInfoCallbacks(this));

    // Auth (Write, Notify)
    pAuthChar = pService->createCharacteristic(
        FEEDME_AUTH_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
    );
    pAuthChar->setCallbacks(new AuthCallbacks(this));

    // Status (Read, Notify)
    pStatusChar = pService->createCharacteristic(
        FEEDME_STATUS_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    pStatusChar->setCallbacks(new StatusCallbacks(this));

    // Settings (Read, Write, Notify) - Notify for chunked data
    pSettingsChar = pService->createCharacteristic(
        FEEDME_SETTINGS_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
    );
    pSettingsChar->setCallbacks(new SettingsCallbacks(this));

    // Feed Schedules (Read, Write, Notify) - Notify for chunked data
    pFeedSchedulesChar = pService->createCharacteristic(
        FEEDME_FEED_SCHEDULES_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
    );
    pFeedSchedulesChar->setCallbacks(new FeedSchedulesCallbacks(this));

    // Feed Command (Write)
    pFeedCmdChar = pService->createCharacteristic(
        FEEDME_FEED_CMD_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    pFeedCmdChar->setCallbacks(new FeedCmdCallbacks(this));

    // History (Read, Notify) - Notify for chunked data
    pHistoryChar = pService->createCharacteristic(
        FEEDME_HISTORY_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    pHistoryChar->setCallbacks(new HistoryCallbacks(this));

    // Time Sync (Write)
    pTimeSyncChar = pService->createCharacteristic(
        FEEDME_TIME_SYNC_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    pTimeSyncChar->setCallbacks(new TimeSyncCallbacks(this));

    // WiFi OTA (Write)
    pWifiOtaChar = pService->createCharacteristic(
        FEEDME_WIFI_OTA_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    pWifiOtaChar->setCallbacks(new WifiOtaCallbacks(this));

    // Set PIN (Write)
    pSetPinChar = pService->createCharacteristic(
        FEEDME_SET_PIN_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    pSetPinChar->setCallbacks(new SetPinCallbacks(this));

    // BLE Schedules (Read, Write, Notify) - Notify for chunked data
    pBleSchedulesChar = pService->createCharacteristic(
        FEEDME_BLE_SCHEDULES_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
    );
    pBleSchedulesChar->setCallbacks(new BleSchedulesCallbacks(this));

    // Public Info (Read) - always accessible, no auth required
    pPublicInfoChar = pService->createCharacteristic(
        FEEDME_PUBLIC_INFO_UUID,
        BLECharacteristic::PROPERTY_READ
    );
    pPublicInfoChar->setCallbacks(new PublicInfoCallbacks(this));

    // Start the service
    pService->start();

    initialized = true;
    DEBUG_PRINTLN("BLE: Initialized with expanded GATT service");
}

void BLEManager::start() {
    if (!initialized) {
        DEBUG_PRINTLN("BLE: Not initialized, cannot start");
        return;
    }

    if (running) {
        return;
    }

    // Pre-populate characteristic values before advertising
    updateCharacteristicValues();

    // Start advertising
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(FEEDME_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);  // Helps with iPhone connection issues
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    running = true;
    DEBUG_PRINTLN("BLE: Advertising started");
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
    resetSession();

    DEBUG_PRINTLN("BLE: Stopped");
}

void BLEManager::deinit() {
    stop();  // Stop advertising first

    if (initialized) {
        BLEDevice::deinit(false);  // false = don't release memory (faster reinit)
        initialized = false;
        pServer = nullptr;
        pDeviceInfoChar = nullptr;
        pAuthChar = nullptr;
        pStatusChar = nullptr;
        pSettingsChar = nullptr;
        pFeedSchedulesChar = nullptr;
        pFeedCmdChar = nullptr;
        pHistoryChar = nullptr;
        pTimeSyncChar = nullptr;
        pWifiOtaChar = nullptr;
        pSetPinChar = nullptr;
        pBleSchedulesChar = nullptr;
        pPublicInfoChar = nullptr;
        DEBUG_PRINTLN("BLE: Deinitialized");
    }
}

void BLEManager::update() {
    // Nothing to do in update for now
    // The callbacks handle everything asynchronously
}

bool BLEManager::isActuallyAdvertising() const {
    if (!initialized || !running) {
        return false;
    }
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    return pAdvertising && pAdvertising->isAdvertising();
}

bool BLEManager::ensureAdvertising() {
    if (!initialized || !running) {
        return false;  // Not supposed to be advertising
    }

    // Check if we think we're running but advertising actually stopped
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    if (pAdvertising && !pAdvertising->isAdvertising() && !clientConnected) {
        DEBUG_PRINTLN("BLE: WARNING - Advertising stopped unexpectedly, restarting!");
        BLEDevice::startAdvertising();
        return true;  // Had to restart
    }
    return false;  // No restart needed
}

void BLEManager::notifyStatus() {
    if (!clientConnected || !pStatusChar) return;

    // Trigger a read to update the value, then notify
    StatusCallbacks cb(this);
    cb.onRead(pStatusChar);
    pStatusChar->notify();
}

void BLEManager::notifyAuth(bool success, uint8_t attemptsRemaining, uint32_t lockoutSeconds) {
    if (!pAuthChar) return;

    JsonDocument doc;
    doc["success"] = success;
    doc["attemptsRemaining"] = attemptsRemaining;
    doc["lockoutSeconds"] = lockoutSeconds;
    // Include flag indicating if PIN is displayed on device
    doc["pinDisplayed"] = (strlen(session.generatedPin) > 0 && !session.authenticated);

    String json;
    serializeJson(doc, json);
    pAuthChar->setValue(json.c_str());
    pAuthChar->notify();
}

void BLEManager::generatePairingPin() {
    // Generate a random 4-digit PIN
    uint32_t randomNum = esp_random() % 10000;
    snprintf(session.generatedPin, sizeof(session.generatedPin), "%04u", randomNum);

    DEBUG_PRINTF("BLE: Generated pairing PIN: %s\n", session.generatedPin);

    // Request PIN display (deferred to main loop to avoid blocking BLE callback)
    // E-paper refresh takes several seconds and can cause issues in BLE callbacks
    display.requestShowPairingPin(session.generatedPin);
}

void BLEManager::clearPairingDisplay() {
    if (strlen(session.generatedPin) > 0) {
        session.generatedPin[0] = '\0';
        // Return display to normal (deferred to main loop to avoid blocking BLE callback)
        display.requestHidePairingPin();
        DEBUG_PRINTLN("BLE: Pairing PIN display clear requested");
    }
}

void BLEManager::updateCharacteristicValues() {
    // Pre-populate characteristic values for large data
    // This is necessary because setting values in onRead callbacks
    // may be too late for BLE long reads

    if (!initialized) return;

    // Feed Schedules
    if (pFeedSchedulesChar) {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        for (int i = 0; i < storage.getScheduleCount(); i++) {
            Schedule* s = storage.getSchedule(i);
            if (!s) continue;

            JsonObject obj = arr.add<JsonObject>();
            obj["id"] = s->id;
            obj["name"] = s->name;
            obj["hour"] = s->hour;
            obj["minute"] = s->minute;
            obj["days"] = s->days;
            obj["enabled"] = s->enabled;
            obj["scheduleType"] = static_cast<uint8_t>(s->scheduleType);
            obj["sunOffset"] = s->sunOffset;
            obj["duration"] = s->duration;
            obj["startMonth"] = s->startMonth;
            obj["startDay"] = s->startDay;
            obj["endMonth"] = s->endMonth;
            obj["endDay"] = s->endDay;
        }

        String json;
        serializeJson(doc, json);
        pFeedSchedulesChar->setValue((uint8_t*)json.c_str(), json.length());
        DEBUG_PRINTF("BLE: Pre-populated Feed Schedules (%d bytes)\n", json.length());
    }

    // BLE Schedules
    if (pBleSchedulesChar) {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        for (int i = 0; i < storage.getBleScheduleCount(); i++) {
            BleSchedule* s = storage.getBleSchedule(i);
            if (!s) continue;

            JsonObject obj = arr.add<JsonObject>();
            obj["id"] = s->id;
            obj["name"] = s->name;
            obj["startHour"] = s->startHour;
            obj["startMinute"] = s->startMinute;
            obj["endHour"] = s->endHour;
            obj["endMinute"] = s->endMinute;
            obj["days"] = s->days;
            obj["enabled"] = s->enabled;
        }

        String json;
        serializeJson(doc, json);
        pBleSchedulesChar->setValue((uint8_t*)json.c_str(), json.length());
        DEBUG_PRINTF("BLE: Pre-populated BLE Schedules (%d bytes)\n", json.length());
    }

    // Feed History
    if (pHistoryChar) {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        for (int i = 0; i < storage.getFeedHistoryCount(); i++) {
            const FeedEvent* event = storage.getFeedEvent(i);
            if (!event) continue;

            JsonObject obj = arr.add<JsonObject>();
            obj["timestamp"] = event->timestamp;
            obj["duration"] = event->duration;
            obj["manual"] = event->manual;
            obj["scheduleName"] = event->scheduleName;
        }

        String json;
        serializeJson(doc, json);
        pHistoryChar->setValue((uint8_t*)json.c_str(), json.length());
        DEBUG_PRINTF("BLE: Pre-populated Feed History (%d bytes)\n", json.length());
    }
}

// =============================================================================
// Chunked Data Transfer for Large Payloads
// =============================================================================
// Protocol: Each chunk has a 2-byte header
// Byte 0: Chunk sequence number (0-based)
// Byte 1: Flags - 0x01 = more chunks follow, 0x00 = last chunk
// Bytes 2+: JSON data
// =============================================================================

void BLEManager::sendChunkedData(BLECharacteristic* pChar, const String& data) {
    if (!pChar || !clientConnected) {
        DEBUG_PRINTLN("BLE: Cannot send chunked data - no client or null characteristic");
        return;
    }

    const size_t CHUNK_SIZE = 500;  // Leave room for header + BLE overhead
    const size_t HEADER_SIZE = 2;
    const size_t DATA_PER_CHUNK = CHUNK_SIZE - HEADER_SIZE;

    size_t totalLen = data.length();
    size_t offset = 0;
    uint8_t chunkNum = 0;

    DEBUG_PRINTF("BLE: Sending %d bytes in chunks\n", totalLen);

    while (offset < totalLen) {
        size_t remaining = totalLen - offset;
        size_t chunkDataLen = (remaining > DATA_PER_CHUNK) ? DATA_PER_CHUNK : remaining;
        bool moreChunks = (offset + chunkDataLen) < totalLen;

        // Build chunk: [sequence][flags][data...]
        uint8_t chunk[CHUNK_SIZE];
        chunk[0] = chunkNum;
        chunk[1] = moreChunks ? 0x01 : 0x00;
        memcpy(&chunk[HEADER_SIZE], data.c_str() + offset, chunkDataLen);

        // Send via notification
        pChar->setValue(chunk, HEADER_SIZE + chunkDataLen);
        pChar->notify();

        DEBUG_PRINTF("BLE: Sent chunk %d (%d bytes, %s)\n",
                      chunkNum, chunkDataLen, moreChunks ? "more" : "last");

        offset += chunkDataLen;
        chunkNum++;

        // Small delay between chunks to allow BLE stack to process
        delay(20);
    }
}

// =============================================================================
// Chunked Write Reception
// =============================================================================
// Protocol (same as chunked reads but for writes from iOS):
// Byte 0: Chunk sequence number (0-based)
// Byte 1: Flags - 0x01 = more chunks follow, 0x00 = last chunk
// Bytes 2+: JSON data
//
// Non-chunked writes start with '[' or '{' (JSON array or object)
// =============================================================================

bool BLEManager::receiveChunkedWrite(const BLEUUID& charUuid, const String& value, String& assembledData) {
    if (value.length() < 2) {
        // Too short to be chunked, treat as complete data
        assembledData = value;
        return true;
    }

    // Check if this looks like chunked data (first two bytes are header, not JSON)
    char firstChar = value.charAt(0);
    char secondChar = value.charAt(1);

    // JSON arrays start with '[', objects with '{', these are typically ASCII 91 and 123
    // Chunked data has sequence number (0-255) and flags (0 or 1) as first two bytes
    // A chunk sequence of '[' (91) or '{' (123) with flags of 0 or 1 is unlikely
    // but we check more carefully: if second byte is 0x00 or 0x01, it's likely chunked
    bool looksChunked = (secondChar == 0x00 || secondChar == 0x01) &&
                        (firstChar != '[' && firstChar != '{');

    if (!looksChunked) {
        // Not chunked - return the data as-is
        assembledData = value;
        writeBuffers.erase(charUuid.toString());  // Clear any partial buffer
        return true;
    }

    // This is chunked data
    uint8_t chunkNum = (uint8_t)firstChar;
    bool moreChunks = (secondChar == 0x01);
    String chunkData = value.substring(2);

    String bufferKey = charUuid.toString();

    // If this is chunk 0, clear any existing buffer
    if (chunkNum == 0) {
        writeBuffers[bufferKey] = "";
    }

    // Append chunk data to buffer
    writeBuffers[bufferKey] += chunkData;

    DEBUG_PRINTF("BLE: Received write chunk %d (%d bytes, %s)\n",
                  chunkNum, chunkData.length(), moreChunks ? "more" : "last");

    if (!moreChunks) {
        // Last chunk - return assembled data
        assembledData = writeBuffers[bufferKey];
        writeBuffers.erase(bufferKey);
        DEBUG_PRINTF("BLE: Assembled %d bytes from chunked writes\n", assembledData.length());
        return true;
    }

    // More chunks expected
    return false;
}
