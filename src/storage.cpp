#include "storage.h"
#include <time.h>

Storage storage;

// Day name abbreviations for auto-generated names
static const char* DAY_ABBREV[] = {"Su", "M", "Tu", "W", "Th", "F", "Sa"};

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

    // Load WiFi schedules
    loadWifiSchedules();

    Serial.printf("Storage: Initialized, device ID: %s, %d schedules, %d WiFi schedules\n",
                  deviceId, scheduleCount, wifiScheduleCount);
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
    settings.batteryGoodThreshold = BATTERY_VOLTAGE_GOOD;
    settings.batteryOkayThreshold = BATTERY_VOLTAGE_OKAY;
    settings.batteryCriticalThreshold = BATTERY_VOLTAGE_CRITICAL;
}

void Storage::loadSettings() {
    settings.motorDuration = prefs.getUChar(PREF_MOTOR_DURATION, MOTOR_DEFAULT_DURATION_SEC);
    settings.vacationMode = prefs.getBool(PREF_VACATION_MODE, false);
    settings.batteryGoodThreshold = prefs.getFloat("batGood", BATTERY_VOLTAGE_GOOD);
    settings.batteryOkayThreshold = prefs.getFloat("batOkay", BATTERY_VOLTAGE_OKAY);
    settings.batteryCriticalThreshold = prefs.getFloat("batCrit", BATTERY_VOLTAGE_CRITICAL);
    strncpy(settings.deviceId, deviceId, 5);
}

void Storage::saveSettings() {
    prefs.putUChar(PREF_MOTOR_DURATION, settings.motorDuration);
    prefs.putBool(PREF_VACATION_MODE, settings.vacationMode);
    prefs.putFloat("batGood", settings.batteryGoodThreshold);
    prefs.putFloat("batOkay", settings.batteryOkayThreshold);
    prefs.putFloat("batCrit", settings.batteryCriticalThreshold);
    Serial.println("Storage: Settings saved");
}

void Storage::loadSchedules() {
    File file = LittleFS.open(SCHEDULES_FILE, "r");
    if (!file) {
        Serial.println("Storage: No schedules file found, creating default");
        scheduleCount = 0;
        // Create default 7:00 AM daily schedule for testing
        Schedule defaultSched = {};
        defaultSched.hour = 7;
        defaultSched.minute = 0;
        defaultSched.days = 0x7F;  // All days
        defaultSched.enabled = true;
        defaultSched.duration = 0;  // Use default
        addSchedule(defaultSched);
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("Storage: Failed to parse schedules: %s\n", error.c_str());
        scheduleCount = 0;
        return;
    }

    JsonArray arr = doc["schedules"].as<JsonArray>();
    scheduleCount = 0;
    nextScheduleId = 1;

    for (JsonObject obj : arr) {
        if (scheduleCount >= MAX_SCHEDULES) break;

        Schedule& s = schedules[scheduleCount];
        s.id = obj["id"] | nextScheduleId;
        if (s.id >= nextScheduleId) {
            nextScheduleId = s.id + 1;
        }

        strlcpy(s.name, obj["name"] | "", sizeof(s.name));
        s.hour = obj["hour"] | 6;
        s.minute = obj["minute"] | 0;
        s.days = obj["days"] | 0x7F;  // Default all days
        s.startMonth = obj["startMonth"] | -1;
        s.startDay = obj["startDay"] | -1;
        s.endMonth = obj["endMonth"] | -1;
        s.endDay = obj["endDay"] | -1;
        s.duration = obj["duration"] | 0;
        s.enabled = obj["enabled"] | true;

        // Generate name if empty
        if (s.name[0] == '\0') {
            s.generateName();
        }

        scheduleCount++;
    }

    Serial.printf("Storage: Loaded %d schedules\n", scheduleCount);
}

void Storage::saveSchedules() {
    JsonDocument doc;
    JsonArray arr = doc["schedules"].to<JsonArray>();

    for (int i = 0; i < scheduleCount; i++) {
        JsonObject obj = arr.add<JsonObject>();
        Schedule& s = schedules[i];

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

    File file = LittleFS.open(SCHEDULES_FILE, "w");
    if (!file) {
        Serial.println("Storage: Failed to open schedules file for writing");
        return;
    }

    serializeJson(doc, file);
    file.close();
    Serial.printf("Storage: Saved %d schedules\n", scheduleCount);
}

Schedule* Storage::getSchedule(int index) {
    if (index < 0 || index >= scheduleCount) {
        return nullptr;
    }
    return &schedules[index];
}

Schedule* Storage::getScheduleById(uint16_t id) {
    for (int i = 0; i < scheduleCount; i++) {
        if (schedules[i].id == id) {
            return &schedules[i];
        }
    }
    return nullptr;
}

bool Storage::addSchedule(const Schedule& schedule) {
    if (scheduleCount >= MAX_SCHEDULES) {
        Serial.println("Storage: Max schedules reached");
        return false;
    }

    schedules[scheduleCount] = schedule;
    schedules[scheduleCount].id = nextScheduleId++;

    // Generate name if empty
    if (schedules[scheduleCount].name[0] == '\0') {
        schedules[scheduleCount].generateName();
    }

    scheduleCount++;
    saveSchedules();
    return true;
}

bool Storage::updateSchedule(uint16_t id, const Schedule& schedule) {
    Schedule* existing = getScheduleById(id);
    if (!existing) {
        return false;
    }

    *existing = schedule;
    existing->id = id;  // Preserve original ID

    if (existing->name[0] == '\0') {
        existing->generateName();
    }

    saveSchedules();
    return true;
}

bool Storage::deleteSchedule(uint16_t id) {
    int index = -1;
    for (int i = 0; i < scheduleCount; i++) {
        if (schedules[i].id == id) {
            index = i;
            break;
        }
    }

    if (index < 0) {
        return false;
    }

    // Shift remaining schedules
    for (int i = index; i < scheduleCount - 1; i++) {
        schedules[i] = schedules[i + 1];
    }
    scheduleCount--;

    saveSchedules();
    return true;
}

String Storage::getSchedulesJson() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (int i = 0; i < scheduleCount; i++) {
        JsonObject obj = arr.add<JsonObject>();
        Schedule& s = schedules[i];

        obj["id"] = s.id;
        obj["name"] = s.name;
        obj["time"] = String(s.hour < 10 ? "0" : "") + s.hour + ":" +
                      String(s.minute < 10 ? "0" : "") + s.minute;
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

    scheduleCount = 0;
    for (JsonObject obj : arr) {
        if (scheduleCount >= MAX_SCHEDULES) break;

        Schedule& s = schedules[scheduleCount];
        s.id = obj["id"] | (scheduleCount + 1);

        strlcpy(s.name, obj["name"] | "", sizeof(s.name));
        s.hour = obj["hour"] | 6;
        s.minute = obj["minute"] | 0;
        s.days = obj["days"] | 0x7F;
        s.startMonth = obj["startMonth"] | -1;
        s.startDay = obj["startDay"] | -1;
        s.endMonth = obj["endMonth"] | -1;
        s.endDay = obj["endDay"] | -1;
        s.duration = obj["duration"] | 0;
        s.enabled = obj["enabled"] | true;

        if (s.name[0] == '\0') {
            s.generateName();
        }

        if (s.id >= nextScheduleId) {
            nextScheduleId = s.id + 1;
        }

        scheduleCount++;
    }

    saveSchedules();
    return true;
}

// Schedule helper methods
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

void Schedule::generateName() {
    char timeStr[8];
    int displayHour = hour;
    const char* ampm = "am";

    if (hour == 0) {
        displayHour = 12;
    } else if (hour == 12) {
        ampm = "pm";
    } else if (hour > 12) {
        displayHour = hour - 12;
        ampm = "pm";
    }

    snprintf(timeStr, sizeof(timeStr), "%d:%02d%s", displayHour, minute, ampm);

    // Determine day pattern
    const char* dayPattern = "";
    if (days == 0x7F) {
        dayPattern = "Daily";
    } else if (days == 0x3E) {  // Mon-Fri (bits 1-5)
        dayPattern = "Weekdays";
    } else if (days == 0x41) {  // Sat-Sun (bits 0,6)
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
    if (scheduleCount == 0) return false;

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

    for (int i = 0; i < scheduleCount; i++) {
        Schedule& s = schedules[i];
        if (!s.enabled) continue;
        if (!s.isActiveOnDate(currentMonth, currentDayOfMonth)) continue;

        // Check each day of the week
        for (int d = 0; d < 7; d++) {
            int checkDay = (currentDay + d) % 7;

            if (!s.isActiveOnDay(checkDay)) continue;

            // Check if this schedule time is still upcoming
            bool isToday = (d == 0);
            bool isPast = isToday && (s.hour < currentHour ||
                         (s.hour == currentHour && s.minute <= currentMin));

            if (isPast) continue;

            // This is a valid upcoming run
            if (d < bestDaysAway ||
                (d == bestDaysAway &&
                 (s.hour < bestHour || (s.hour == bestHour && s.minute < bestMinute)))) {
                bestDaysAway = d;
                bestHour = s.hour;
                bestMinute = s.minute;
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

// WifiSchedule helper methods
bool WifiSchedule::isActiveOnDay(uint8_t dayOfWeek) const {
    if (dayOfWeek > 6) return false;
    return (days & (1 << dayOfWeek)) != 0;
}

bool WifiSchedule::isActiveNow(int hour, int minute, int dayOfWeek) const {
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

// WiFi Schedules storage
void Storage::loadWifiSchedules() {
    File file = LittleFS.open("/wifi_schedules.json", "r");
    if (!file) {
        Serial.println("Storage: No WiFi schedules file found");
        wifiScheduleCount = 0;
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("Storage: Failed to parse WiFi schedules: %s\n", error.c_str());
        wifiScheduleCount = 0;
        return;
    }

    JsonArray arr = doc["wifiSchedules"].as<JsonArray>();
    wifiScheduleCount = 0;
    nextWifiScheduleId = 1;

    for (JsonObject obj : arr) {
        if (wifiScheduleCount >= MAX_WIFI_SCHEDULES) break;

        WifiSchedule& s = wifiSchedules[wifiScheduleCount];
        s.id = obj["id"] | nextWifiScheduleId;
        if (s.id >= nextWifiScheduleId) {
            nextWifiScheduleId = s.id + 1;
        }

        strlcpy(s.name, obj["name"] | "", sizeof(s.name));
        s.startHour = obj["startHour"] | 6;
        s.startMinute = obj["startMinute"] | 0;
        s.endHour = obj["endHour"] | 8;
        s.endMinute = obj["endMinute"] | 0;
        s.days = obj["days"] | 0x7F;
        s.enabled = obj["enabled"] | true;

        wifiScheduleCount++;
    }

    Serial.printf("Storage: Loaded %d WiFi schedules\n", wifiScheduleCount);
}

void Storage::saveWifiSchedules() {
    JsonDocument doc;
    JsonArray arr = doc["wifiSchedules"].to<JsonArray>();

    for (int i = 0; i < wifiScheduleCount; i++) {
        JsonObject obj = arr.add<JsonObject>();
        WifiSchedule& s = wifiSchedules[i];

        obj["id"] = s.id;
        obj["name"] = s.name;
        obj["startHour"] = s.startHour;
        obj["startMinute"] = s.startMinute;
        obj["endHour"] = s.endHour;
        obj["endMinute"] = s.endMinute;
        obj["days"] = s.days;
        obj["enabled"] = s.enabled;
    }

    File file = LittleFS.open("/wifi_schedules.json", "w");
    if (!file) {
        Serial.println("Storage: Failed to open WiFi schedules file for writing");
        return;
    }

    serializeJson(doc, file);
    file.close();
    Serial.printf("Storage: Saved %d WiFi schedules\n", wifiScheduleCount);
}

WifiSchedule* Storage::getWifiSchedule(int index) {
    if (index < 0 || index >= wifiScheduleCount) {
        return nullptr;
    }
    return &wifiSchedules[index];
}

WifiSchedule* Storage::getWifiScheduleById(uint16_t id) {
    for (int i = 0; i < wifiScheduleCount; i++) {
        if (wifiSchedules[i].id == id) {
            return &wifiSchedules[i];
        }
    }
    return nullptr;
}

bool Storage::addWifiSchedule(const WifiSchedule& schedule) {
    if (wifiScheduleCount >= MAX_WIFI_SCHEDULES) {
        Serial.println("Storage: Max WiFi schedules reached");
        return false;
    }

    wifiSchedules[wifiScheduleCount] = schedule;
    wifiSchedules[wifiScheduleCount].id = nextWifiScheduleId++;
    wifiScheduleCount++;
    saveWifiSchedules();
    return true;
}

bool Storage::updateWifiSchedule(uint16_t id, const WifiSchedule& schedule) {
    WifiSchedule* existing = getWifiScheduleById(id);
    if (!existing) {
        return false;
    }

    *existing = schedule;
    existing->id = id;  // Preserve original ID
    saveWifiSchedules();
    return true;
}

bool Storage::deleteWifiSchedule(uint16_t id) {
    int index = -1;
    for (int i = 0; i < wifiScheduleCount; i++) {
        if (wifiSchedules[i].id == id) {
            index = i;
            break;
        }
    }

    if (index < 0) {
        return false;
    }

    // Shift remaining schedules
    for (int i = index; i < wifiScheduleCount - 1; i++) {
        wifiSchedules[i] = wifiSchedules[i + 1];
    }
    wifiScheduleCount--;

    saveWifiSchedules();
    return true;
}

String Storage::getWifiSchedulesJson() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (int i = 0; i < wifiScheduleCount; i++) {
        JsonObject obj = arr.add<JsonObject>();
        WifiSchedule& s = wifiSchedules[i];

        obj["id"] = s.id;
        obj["name"] = s.name;
        obj["startTime"] = String(s.startHour < 10 ? "0" : "") + s.startHour + ":" +
                           String(s.startMinute < 10 ? "0" : "") + s.startMinute;
        obj["endTime"] = String(s.endHour < 10 ? "0" : "") + s.endHour + ":" +
                         String(s.endMinute < 10 ? "0" : "") + s.endMinute;
        obj["startHour"] = s.startHour;
        obj["startMinute"] = s.startMinute;
        obj["endHour"] = s.endHour;
        obj["endMinute"] = s.endMinute;
        obj["days"] = s.days;
        obj["enabled"] = s.enabled;
    }

    String result;
    serializeJson(doc, result);
    return result;
}

bool Storage::shouldWifiBeActive() {
    if (wifiScheduleCount == 0) {
        return false;  // No schedules means WiFi not auto-enabled
    }

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return false;  // Can't determine time
    }

    int hour = timeinfo.tm_hour;
    int minute = timeinfo.tm_min;
    int dayOfWeek = timeinfo.tm_wday;

    for (int i = 0; i < wifiScheduleCount; i++) {
        if (wifiSchedules[i].isActiveNow(hour, minute, dayOfWeek)) {
            return true;
        }
    }

    return false;
}

void Storage::resetToDefaults() {
    Serial.println("Storage: Resetting to defaults");

    // Clear all schedules
    scheduleCount = 0;
    nextScheduleId = 1;

    // Clear WiFi schedules
    wifiScheduleCount = 0;
    nextWifiScheduleId = 1;

    // Delete schedules files
    LittleFS.remove(SCHEDULES_FILE);
    LittleFS.remove("/wifi_schedules.json");

    // Reset settings to defaults
    initDefaultSettings();
    saveSettings();

    // Create default schedule
    Schedule defaultSched = {};
    defaultSched.hour = 7;
    defaultSched.minute = 0;
    defaultSched.days = 0x7F;  // All days
    defaultSched.enabled = true;
    defaultSched.duration = 0;
    addSchedule(defaultSched);

    Serial.println("Storage: Reset complete");
}
