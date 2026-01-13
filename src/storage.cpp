#include "storage.h"
#include <time.h>

Storage storage;

// =============================================================================
// Serialization helpers for ScheduleManager
// =============================================================================

void Storage::deserializeFeedSchedule(Schedule& s, JsonObject& obj) {
    s.id = obj["id"] | 0;
    strlcpy(s.name, obj["name"] | "", sizeof(s.name));
    s.hour = obj["hour"] | 6;
    s.minute = obj["minute"] | 0;
    s.days = obj["days"] | DAYS_ALL;
    s.startMonth = obj["startMonth"] | -1;
    s.startDay = obj["startDay"] | -1;
    s.endMonth = obj["endMonth"] | -1;
    s.endDay = obj["endDay"] | -1;
    s.duration = obj["duration"] | 0;
    s.enabled = obj["enabled"] | true;

    // Name is generated when schedule is added/updated, not during deserialization
}

void Storage::serializeFeedSchedule(const Schedule& s, JsonObject& obj) {
    obj["id"] = s.id;
    obj["name"] = s.name;
    obj["hour"] = s.hour;
    obj["minute"] = s.minute;
    obj["days"] = s.days;
    obj["startMonth"] = s.startMonth;
    obj["startDay"] = s.startDay;
    obj["endMonth"] = s.endMonth;
    obj["endDay"] = s.endDay;
    obj["duration"] = s.duration;
    obj["enabled"] = s.enabled;
}

void Storage::deserializeBleSchedule(BleSchedule& s, JsonObject& obj) {
    s.id = obj["id"] | 0;
    strlcpy(s.name, obj["name"] | "", sizeof(s.name));
    s.startHour = obj["startHour"] | 4;
    s.startMinute = obj["startMinute"] | 0;
    s.endHour = obj["endHour"] | 20;
    s.endMinute = obj["endMinute"] | 0;
    s.days = obj["days"] | DAYS_ALL;
    s.enabled = obj["enabled"] | true;
}

void Storage::serializeBleSchedule(const BleSchedule& s, JsonObject& obj) {
    obj["id"] = s.id;
    obj["name"] = s.name;
    obj["startHour"] = s.startHour;
    obj["startMinute"] = s.startMinute;
    obj["endHour"] = s.endHour;
    obj["endMinute"] = s.endMinute;
    obj["days"] = s.days;
    obj["enabled"] = s.enabled;
}

// =============================================================================
// Storage initialization
// =============================================================================

bool Storage::begin() {
    // Initialize preferences
    if (!prefs.begin(PREF_NAMESPACE, false)) {
        Serial.println("Storage: Failed to initialize preferences");
        return false;
    }

    // Initialize LittleFS
    if (!LittleFS.begin(true)) {  // true = format if mount fails
        Serial.println("Storage: Failed to mount LittleFS");
        return false;
    }

    // Load or generate device ID
    getDeviceId();

    // Load settings
    loadSettings();

    // Load schedules
    loadSchedules();

    // Load BLE schedules
    loadBleSchedules();

    // Load feed history
    loadFeedHistory();

    Serial.printf("Storage: Initialized, device ID: %s, %d schedules, %d BLE schedules, %d history entries\n",
                  deviceId, getScheduleCount(), getBleScheduleCount(), getFeedHistoryCount());
    return true;
}

const char* Storage::getDeviceId() {
    if (deviceId[0] == '\0') {
        // Try to load from preferences
        String stored = prefs.getString(PREF_DEVICE_ID, "");
        if (stored.length() == 4) {
            strncpy(deviceId, stored.c_str(), 5);
        } else {
            // Generate new ID
            generateDeviceId();
            prefs.putString(PREF_DEVICE_ID, deviceId);
            Serial.printf("Storage: Generated new device ID: %s\n", deviceId);
        }
    }
    return deviceId;
}

void Storage::generateDeviceId() {
    // Generate random 4-digit alphanumeric ID
    const char charset[] = "0123456789ABCDEFGHJKLMNPQRSTUVWXYZ";  // Omit I, O for clarity
    randomSeed(esp_random());

    for (int i = 0; i < 4; i++) {
        deviceId[i] = charset[random(0, sizeof(charset) - 1)];
    }
    deviceId[4] = '\0';
}

void Storage::initDefaultSettings() {
    strncpy(settings.deviceId, deviceId, 5);
    settings.motorDuration = MOTOR_DEFAULT_DURATION_SEC;
    settings.vacationMode = false;
    settings.sleepTimeout = DEFAULT_SLEEP_TIMEOUT;
    settings.timezoneOffset = 0;  // UTC
    settings.batteryType = BatteryType::SLA;  // Default to SLA
}

void Storage::loadSettings() {
    settings.motorDuration = prefs.getUChar(PREF_MOTOR_DURATION, MOTOR_DEFAULT_DURATION_SEC);
    settings.vacationMode = prefs.getBool(PREF_VACATION_MODE, false);
    settings.sleepTimeout = static_cast<SleepTimeout>(prefs.getUChar("sleepTmout", static_cast<uint8_t>(DEFAULT_SLEEP_TIMEOUT)));
    settings.timezoneOffset = prefs.getShort("tzOffset", 0);  // Default to UTC
    settings.batteryType = static_cast<BatteryType>(prefs.getUChar(PREF_BATTERY_TYPE, static_cast<uint8_t>(BatteryType::SLA)));
    strncpy(settings.deviceId, deviceId, 5);
}

void Storage::saveSettings() {
    prefs.putUChar(PREF_MOTOR_DURATION, settings.motorDuration);
    prefs.putBool(PREF_VACATION_MODE, settings.vacationMode);
    prefs.putUChar("sleepTmout", static_cast<uint8_t>(settings.sleepTimeout));
    prefs.putShort("tzOffset", settings.timezoneOffset);
    prefs.putUChar(PREF_BATTERY_TYPE, static_cast<uint8_t>(settings.batteryType));
    Serial.println("Storage: Settings saved");
}

// =============================================================================
// Feed Schedules - delegate to ScheduleManager
// =============================================================================

void Storage::loadSchedules() {
    feedSchedules.load(SCHEDULES_FILE, "schedules", deserializeFeedSchedule);

    // Create default schedule if none exist
    if (feedSchedules.getCount() == 0) {
        Serial.println("Storage: No schedules found, creating default");
        Schedule defaultSched = {};
        defaultSched.hour = 13;  // 13:00 UTC = 7:00 AM CST / 8:00 AM EST
        defaultSched.minute = 0;
        defaultSched.days = DAYS_ALL;
        defaultSched.enabled = true;
        defaultSched.duration = 0;
        defaultSched.startMonth = -1;
        defaultSched.startDay = -1;
        defaultSched.endMonth = -1;
        defaultSched.endDay = -1;
        addSchedule(defaultSched);
    }
}

void Storage::saveSchedules() {
    feedSchedules.save(SCHEDULES_FILE, "schedules", serializeFeedSchedule);
}

Schedule* Storage::getSchedule(int index) {
    return feedSchedules.get(index);
}

Schedule* Storage::getScheduleById(uint16_t id) {
    return feedSchedules.getById(id);
}

bool Storage::addSchedule(const Schedule& schedule) {
    Schedule s = schedule;
    // Generate name if empty, using local time
    if (s.name[0] == '\0') {
        s.generateName(settings.timezoneOffset);
    }
    bool result = feedSchedules.add(s);
    if (result) {
        saveSchedules();
    }
    return result;
}

bool Storage::updateSchedule(uint16_t id, const Schedule& schedule) {
    Schedule s = schedule;
    if (s.name[0] == '\0') {
        s.generateName(settings.timezoneOffset);
    }
    bool result = feedSchedules.update(id, s);
    if (result) {
        saveSchedules();
    }
    return result;
}

bool Storage::deleteSchedule(uint16_t id) {
    bool result = feedSchedules.remove(id);
    if (result) {
        saveSchedules();
    }
    return result;
}

String Storage::getSchedulesJson() {
    // Return with additional formatting for API response
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (int i = 0; i < getScheduleCount(); i++) {
        Schedule* s = getSchedule(i);
        if (!s) continue;

        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = s->id;
        obj["name"] = s->name;
        obj["time"] = String(s->hour < 10 ? "0" : "") + s->hour + ":" +
                      String(s->minute < 10 ? "0" : "") + s->minute;
        obj["hour"] = s->hour;
        obj["minute"] = s->minute;
        obj["days"] = s->days;
        obj["startMonth"] = s->startMonth;
        obj["startDay"] = s->startDay;
        obj["endMonth"] = s->endMonth;
        obj["endDay"] = s->endDay;
        obj["duration"] = s->duration;
        obj["enabled"] = s->enabled;
    }

    String result;
    serializeJson(doc, result);
    return result;
}

bool Storage::setSchedulesFromJson(const String& json) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        Serial.printf("Storage: Failed to parse JSON: %s\n", error.c_str());
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (!arr) {
        Serial.println("Storage: JSON is not an array");
        return false;
    }

    feedSchedules.clear();
    for (JsonObject obj : arr) {
        Schedule s;
        deserializeFeedSchedule(s, obj);
        feedSchedules.add(s);
    }

    saveSchedules();
    return true;
}

// =============================================================================
// Schedule helper methods
// =============================================================================

bool Schedule::isActiveOnDay(uint8_t dayOfWeek) const {
    if (dayOfWeek > 6) return false;
    return (days & (1 << dayOfWeek)) != 0;
}

bool Schedule::isActiveOnDate(int month, int day) const {
    // If no date range set, always active
    if (startMonth < 0 && endMonth < 0) {
        return true;
    }

    int currentDate = month * 100 + day;  // MMDD format for easy comparison

    if (startMonth > 0 && endMonth > 0) {
        int startDate = startMonth * 100 + startDay;
        int endDate = endMonth * 100 + endDay;

        if (startDate <= endDate) {
            // Normal range (e.g., Mar-Nov)
            return currentDate >= startDate && currentDate <= endDate;
        } else {
            // Wrapping range (e.g., Oct-Feb)
            return currentDate >= startDate || currentDate <= endDate;
        }
    }

    return true;
}

void Schedule::generateName(int16_t tzOffset) {
    char timeStr[8];

    // Convert UTC hour to local time for display
    int localHour = hour - (tzOffset / 60);
    localHour = (localHour + 24) % 24;

    int displayHour = localHour;
    const char* ampm = "am";

    if (localHour == 0) {
        displayHour = 12;
    } else if (localHour == 12) {
        ampm = "pm";
    } else if (localHour > 12) {
        displayHour = localHour - 12;
        ampm = "pm";
    }

    snprintf(timeStr, sizeof(timeStr), "%d:%02d%s", displayHour, minute, ampm);

    // Determine day pattern
    const char* dayPattern = "";
    if (days == DAYS_ALL) {
        dayPattern = "Daily";
    } else if (days == DAYS_WEEKDAYS) {
        dayPattern = "Weekdays";
    } else if (days == DAYS_WEEKENDS) {
        dayPattern = "Weekends";
    } else {
        // Build custom day string
        static char customDays[20];
        customDays[0] = '\0';
        for (int i = 0; i < 7; i++) {
            if (days & (1 << i)) {
                if (customDays[0] != '\0') {
                    strlcat(customDays, "/", sizeof(customDays));
                }
                strlcat(customDays, DAY_ABBREV[i], sizeof(customDays));
            }
        }
        dayPattern = customDays;
    }

    snprintf(name, sizeof(name), "%s at %s", dayPattern, timeStr);
}

bool Storage::getNextRunTime(int& hour, int& minute, int& daysAway) {
    if (getScheduleCount() == 0) return false;

    // Get current time
    struct tm timeinfo;
    time_t now = time(nullptr);
    localtime_r(&now, &timeinfo);

    int currentDay = timeinfo.tm_wday;  // 0 = Sunday
    int currentHour = timeinfo.tm_hour;
    int currentMin = timeinfo.tm_min;
    int currentMonth = timeinfo.tm_mon + 1;
    int currentDayOfMonth = timeinfo.tm_mday;

    int bestDaysAway = 8;  // More than a week = not found
    int bestHour = -1;
    int bestMinute = -1;

    for (int i = 0; i < getScheduleCount(); i++) {
        Schedule* s = getSchedule(i);
        if (!s || !s->enabled) continue;
        if (!s->isActiveOnDate(currentMonth, currentDayOfMonth)) continue;

        // Check each day of the week
        for (int d = 0; d < 7; d++) {
            int checkDay = (currentDay + d) % 7;

            if (!s->isActiveOnDay(checkDay)) continue;

            // Check if this schedule time is still upcoming
            bool isToday = (d == 0);
            bool isPast = isToday && (s->hour < currentHour ||
                         (s->hour == currentHour && s->minute <= currentMin));

            if (isPast) continue;

            // This is a valid upcoming run
            if (d < bestDaysAway ||
                (d == bestDaysAway &&
                 (s->hour < bestHour || (s->hour == bestHour && s->minute < bestMinute)))) {
                bestDaysAway = d;
                bestHour = s->hour;
                bestMinute = s->minute;
            }
            break;  // Found the next occurrence for this schedule
        }
    }

    if (bestHour >= 0) {
        hour = bestHour;
        minute = bestMinute;
        daysAway = bestDaysAway;
        return true;
    }
    return false;
}

// =============================================================================
// BLE Schedules - delegate to ScheduleManager
// =============================================================================

void Storage::loadBleSchedules() {
    bleSchedules.load(BLE_SCHEDULES_FILE, "bleSchedules", deserializeBleSchedule);
}

void Storage::saveBleSchedules() {
    bleSchedules.save(BLE_SCHEDULES_FILE, "bleSchedules", serializeBleSchedule);
}

BleSchedule* Storage::getBleSchedule(int index) {
    return bleSchedules.get(index);
}

BleSchedule* Storage::getBleScheduleById(uint16_t id) {
    return bleSchedules.getById(id);
}

bool Storage::addBleSchedule(const BleSchedule& schedule) {
    bool result = bleSchedules.add(schedule);
    if (result) {
        saveBleSchedules();
    }
    return result;
}

bool Storage::updateBleSchedule(uint16_t id, const BleSchedule& schedule) {
    bool result = bleSchedules.update(id, schedule);
    if (result) {
        saveBleSchedules();
    }
    return result;
}

bool Storage::deleteBleSchedule(uint16_t id) {
    bool result = bleSchedules.remove(id);
    if (result) {
        saveBleSchedules();
    }
    return result;
}

String Storage::getBleSchedulesJson() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (int i = 0; i < getBleScheduleCount(); i++) {
        BleSchedule* s = getBleSchedule(i);
        if (!s) continue;

        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = s->id;
        obj["name"] = s->name;
        obj["startTime"] = String(s->startHour < 10 ? "0" : "") + s->startHour + ":" +
                           String(s->startMinute < 10 ? "0" : "") + s->startMinute;
        obj["endTime"] = String(s->endHour < 10 ? "0" : "") + s->endHour + ":" +
                         String(s->endMinute < 10 ? "0" : "") + s->endMinute;
        obj["startHour"] = s->startHour;
        obj["startMinute"] = s->startMinute;
        obj["endHour"] = s->endHour;
        obj["endMinute"] = s->endMinute;
        obj["days"] = s->days;
        obj["enabled"] = s->enabled;
    }

    String result;
    serializeJson(doc, result);
    return result;
}

// =============================================================================
// BleSchedule helper methods
// =============================================================================

bool BleSchedule::isActiveOnDay(uint8_t dayOfWeek) const {
    if (dayOfWeek > 6) return false;
    return (days & (1 << dayOfWeek)) != 0;
}

bool BleSchedule::isActiveNow(int hour, int minute, int dayOfWeek) const {
    if (!enabled || !isActiveOnDay(dayOfWeek)) {
        return false;
    }

    int currentMins = hour * 60 + minute;
    int startMins = startHour * 60 + startMinute;
    int endMins = endHour * 60 + endMinute;

    if (startMins <= endMins) {
        // Normal range (e.g., 08:00 - 17:00)
        return currentMins >= startMins && currentMins < endMins;
    } else {
        // Overnight range (e.g., 22:00 - 06:00)
        return currentMins >= startMins || currentMins < endMins;
    }
}

bool Storage::shouldBleBeActive() {
    // If no BLE schedules configured, default to always-on
    if (getBleScheduleCount() == 0) {
        return true;
    }

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 0)) {  // 0 = no blocking timeout
        return true;  // Can't determine time, keep BLE on
    }

    int hour = timeinfo.tm_hour;
    int minute = timeinfo.tm_min;
    int dayOfWeek = timeinfo.tm_wday;

    for (int i = 0; i < getBleScheduleCount(); i++) {
        BleSchedule* s = getBleSchedule(i);
        if (s && s->isActiveNow(hour, minute, dayOfWeek)) {
            return true;
        }
    }

    return false;
}

// =============================================================================
// Reset to defaults
// =============================================================================

void Storage::resetToDefaults() {
    Serial.println("Storage: Resetting to defaults");

    // Clear all schedules
    feedSchedules.clear();
    bleSchedules.clear();

    // Delete schedules files
    LittleFS.remove(SCHEDULES_FILE);
    LittleFS.remove(BLE_SCHEDULES_FILE);
    LittleFS.remove(FEED_HISTORY_FILE);

    // Clear feed history
    feedHistoryCount = 0;
    feedHistoryHead = 0;

    // Reset settings to defaults
    initDefaultSettings();
    saveSettings();

    // Create default schedule
    Schedule defaultSched = {};
    defaultSched.hour = 13;  // 13:00 UTC = 7:00 AM CST / 8:00 AM EST
    defaultSched.minute = 0;
    defaultSched.days = DAYS_ALL;
    defaultSched.enabled = true;
    defaultSched.duration = 0;
    defaultSched.startMonth = -1;
    defaultSched.startDay = -1;
    defaultSched.endMonth = -1;
    defaultSched.endDay = -1;
    addSchedule(defaultSched);

    Serial.println("Storage: Reset complete");
}

// =============================================================================
// Feed History
// =============================================================================

void Storage::loadFeedHistory() {
    File file = LittleFS.open(FEED_HISTORY_FILE, "r");
    if (!file) {
        Serial.println("Storage: No feed history file found");
        feedHistoryCount = 0;
        feedHistoryHead = 0;
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("Storage: Failed to parse feed history: %s\n", error.c_str());
        feedHistoryCount = 0;
        feedHistoryHead = 0;
        return;
    }

    JsonArray arr = doc["history"].as<JsonArray>();
    if (!arr) {
        feedHistoryCount = 0;
        feedHistoryHead = 0;
        return;
    }

    feedHistoryCount = 0;
    feedHistoryHead = 0;

    // Load events in order (most recent first)
    for (JsonObject obj : arr) {
        if (feedHistoryCount >= MAX_FEED_HISTORY) break;

        FeedEvent& event = feedHistory[feedHistoryCount];
        event.timestamp = obj["timestamp"] | 0;
        event.duration = obj["duration"] | 0;
        event.manual = obj["manual"] | false;
        strlcpy(event.scheduleName, obj["scheduleName"] | "", sizeof(event.scheduleName));

        feedHistoryCount++;
    }

    Serial.printf("Storage: Loaded %d feed history entries\n", feedHistoryCount);
}

void Storage::saveFeedHistory() {
    File file = LittleFS.open(FEED_HISTORY_FILE, "w");
    if (!file) {
        Serial.println("Storage: Failed to open feed history file for writing");
        return;
    }

    JsonDocument doc;
    JsonArray arr = doc["history"].to<JsonArray>();

    // Save events in order (most recent first)
    for (int i = 0; i < feedHistoryCount; i++) {
        const FeedEvent* event = getFeedEvent(i);
        if (!event) continue;

        JsonObject obj = arr.add<JsonObject>();
        obj["timestamp"] = event->timestamp;
        obj["duration"] = event->duration;
        obj["manual"] = event->manual;
        obj["scheduleName"] = event->scheduleName;
    }

    serializeJson(doc, file);
    file.close();

    Serial.printf("Storage: Saved %d feed history entries\n", feedHistoryCount);
}

void Storage::logFeedEvent(uint8_t duration, bool manual, const char* scheduleName) {
    // Get current Unix timestamp
    time_t now = time(nullptr);

    // Move head to next slot (circular buffer)
    if (feedHistoryCount > 0) {
        feedHistoryHead = (feedHistoryHead + 1) % MAX_FEED_HISTORY;
    }

    FeedEvent& event = feedHistory[feedHistoryHead];
    event.timestamp = (uint32_t)now;
    event.duration = duration;
    event.manual = manual;
    if (scheduleName && scheduleName[0] != '\0') {
        strlcpy(event.scheduleName, scheduleName, sizeof(event.scheduleName));
    } else {
        event.scheduleName[0] = '\0';
    }

    if (feedHistoryCount < MAX_FEED_HISTORY) {
        feedHistoryCount++;
    }

    Serial.printf("Storage: Logged feed event (duration=%ds, manual=%s, schedule=%s)\n",
                  duration, manual ? "yes" : "no", scheduleName ? scheduleName : "");

    saveFeedHistory();
}

const FeedEvent* Storage::getFeedEvent(int index) const {
    if (index < 0 || index >= feedHistoryCount) {
        return nullptr;
    }

    // Circular buffer: head points to most recent, index 0 = most recent
    int actualIndex = (feedHistoryHead - index + MAX_FEED_HISTORY) % MAX_FEED_HISTORY;
    return &feedHistory[actualIndex];
}

String Storage::getFeedHistoryJson() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (int i = 0; i < feedHistoryCount; i++) {
        const FeedEvent* event = getFeedEvent(i);
        if (!event) continue;

        JsonObject obj = arr.add<JsonObject>();
        obj["timestamp"] = event->timestamp;
        obj["duration"] = event->duration;
        obj["manual"] = event->manual;
        obj["scheduleName"] = event->scheduleName;
    }

    String result;
    serializeJson(doc, result);
    return result;
}
