#include "storage.h"
#include "sun_calc.h"
#include "time_format.h"
#include <time.h>
#include <RTClib.h>  // For DateTime
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>

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
    s.scheduleType = static_cast<ScheduleType>(obj["scheduleType"] | 0);  // Default to SPECIFIC_TIME
    s.sunOffset = obj["sunOffset"] | 0;  // Default to 0 minutes offset

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
    obj["scheduleType"] = static_cast<uint8_t>(s.scheduleType);
    obj["sunOffset"] = s.sunOffset;
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
        DEBUG_PRINTLN("Storage: Failed to initialize preferences");
        return false;
    }

    // Initialize LittleFS
    if (!LittleFS.begin(true)) {  // true = format if mount fails
        DEBUG_PRINTLN("Storage: Failed to mount LittleFS");
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

    DEBUG_PRINTF("Storage: Initialized, device ID: %s, %d schedules, %d BLE schedules, %d history entries\n",
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
            DEBUG_PRINTF("Storage: Generated new device ID: %s\n", deviceId);
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
    settings.deviceName[0] = '\0';  // Empty = use default "FeedMe-XXXX"
    settings.motorDuration = MOTOR_DEFAULT_DURATION_SEC;
    settings.vacationMode = false;
    settings.sleepTimeout = DEFAULT_SLEEP_TIMEOUT;
    settings.timezoneOffset = 0;  // UTC (deprecated, use posixTz)
    settings.posixTz[0] = '\0';   // Empty = use timezoneOffset fallback
    settings.batteryType = BatteryType::SLA;  // Default to SLA
    settings.antennaType = DEFAULT_ANTENNA_TYPE;  // Default to rod antenna
    settings.latitude = 0.0f;
    settings.longitude = 0.0f;
    settings.locationSet = false;
    settings.inactivityTimeoutMin = DEFAULT_INACTIVITY_TIMEOUT_MIN;
}

void Storage::loadSettings() {
    settings.motorDuration = prefs.getUChar(PREF_MOTOR_DURATION, MOTOR_DEFAULT_DURATION_SEC);
    settings.vacationMode = prefs.getBool(PREF_VACATION_MODE, false);
    settings.sleepTimeout = static_cast<SleepTimeout>(prefs.getUChar("sleepTmout", static_cast<uint8_t>(DEFAULT_SLEEP_TIMEOUT)));
    settings.timezoneOffset = prefs.getShort("tzOffset", 0);  // Default to UTC (deprecated)
    settings.batteryType = static_cast<BatteryType>(prefs.getUChar(PREF_BATTERY_TYPE, static_cast<uint8_t>(BatteryType::SLA)));
    settings.antennaType = static_cast<AntennaType>(prefs.getUChar(PREF_ANTENNA_TYPE, static_cast<uint8_t>(DEFAULT_ANTENNA_TYPE)));
    settings.latitude = prefs.getFloat(PREF_LATITUDE, 0.0f);
    settings.longitude = prefs.getFloat(PREF_LONGITUDE, 0.0f);
    settings.locationSet = prefs.getBool(PREF_LOCATION_SET, false);
    settings.inactivityTimeoutMin = prefs.getUChar("inactTmout", DEFAULT_INACTIVITY_TIMEOUT_MIN);
    strncpy(settings.deviceId, deviceId, 5);

    // Load device name (empty string = use default "FeedMe-XXXX")
    size_t nameLen = prefs.getString("deviceName", settings.deviceName, sizeof(settings.deviceName));
    if (nameLen == 0) {
        settings.deviceName[0] = '\0';
    }

    // Load POSIX timezone string (empty = use timezoneOffset fallback)
    // Ensure null termination even if NVS contains garbage
    memset(settings.posixTz, 0, sizeof(settings.posixTz));
    prefs.getString("posixTz", settings.posixTz, sizeof(settings.posixTz) - 1);
}

void Storage::saveSettings() {
    prefs.putUChar(PREF_MOTOR_DURATION, settings.motorDuration);
    prefs.putBool(PREF_VACATION_MODE, settings.vacationMode);
    prefs.putUChar("sleepTmout", static_cast<uint8_t>(settings.sleepTimeout));
    prefs.putShort("tzOffset", settings.timezoneOffset);
    prefs.putString("posixTz", settings.posixTz);
    prefs.putUChar(PREF_BATTERY_TYPE, static_cast<uint8_t>(settings.batteryType));
    prefs.putUChar(PREF_ANTENNA_TYPE, static_cast<uint8_t>(settings.antennaType));
    prefs.putFloat(PREF_LATITUDE, settings.latitude);
    prefs.putFloat(PREF_LONGITUDE, settings.longitude);
    prefs.putBool(PREF_LOCATION_SET, settings.locationSet);
    prefs.putUChar("inactTmout", settings.inactivityTimeoutMin);
    prefs.putString("deviceName", settings.deviceName);
    DEBUG_PRINTLN("Storage: Settings saved");
}

void Storage::applyTimezone() {
    // Apply POSIX timezone string to system so localtime() handles DST automatically
    // If posixTz is empty or invalid, fall back to simple UTC offset (no DST)

    // Validate posixTz - must be non-empty, printable ASCII, reasonable length
    bool validPosixTz = false;
    if (settings.posixTz[0] != '\0') {
        validPosixTz = true;
        for (int i = 0; i < (int)sizeof(settings.posixTz) && settings.posixTz[i] != '\0'; i++) {
            char c = settings.posixTz[i];
            // Valid POSIX TZ chars: alphanumeric, +, -, :, /, comma
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '+' || c == '-' ||
                  c == ':' || c == '/' || c == ',' || c == '.')) {
                validPosixTz = false;
                DEBUG_PRINTF("Storage: Invalid char in posixTz at pos %d: 0x%02X\n", i, (unsigned char)c);
                break;
            }
        }
    }

    if (validPosixTz) {
        setenv("TZ", settings.posixTz, 1);
        tzset();
        DEBUG_PRINTF("Storage: Applied POSIX timezone: %s\n", settings.posixTz);
    } else if (settings.timezoneOffset != 0) {
        // Fallback: create simple UTC offset string (no DST support)
        // Format: "UTC+HH:MM" or "UTC-HH:MM" (note: POSIX uses inverted sign)
        int offsetMinutes = -settings.timezoneOffset;  // Invert for POSIX convention
        int hours = abs(offsetMinutes) / 60;
        int mins = abs(offsetMinutes) % 60;
        char tzStr[16];
        if (mins == 0) {
            snprintf(tzStr, sizeof(tzStr), "UTC%+d", offsetMinutes >= 0 ? hours : -hours);
        } else {
            snprintf(tzStr, sizeof(tzStr), "UTC%+d:%02d", offsetMinutes >= 0 ? hours : -hours, mins);
        }
        setenv("TZ", tzStr, 1);
        tzset();
        DEBUG_PRINTF("Storage: Applied fallback timezone: %s (offset %d min)\n", tzStr, settings.timezoneOffset);
    } else {
        // Default to UTC
        setenv("TZ", "UTC0", 1);
        tzset();
        DEBUG_PRINTLN("Storage: Applied UTC timezone");
    }
}

// =============================================================================
// Feed Schedules - delegate to ScheduleManager
// =============================================================================

void Storage::loadSchedules() {
    feedSchedules.load(SCHEDULES_FILE, "schedules", deserializeFeedSchedule);
    DEBUG_PRINTF("Storage: Loaded %d feed schedules\n", feedSchedules.getCount());
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
        char timeStr[6];
        formatTime24Hour(timeStr, sizeof(timeStr), s->hour, s->minute);
        obj["time"] = timeStr;
        obj["hour"] = s->hour;
        obj["minute"] = s->minute;
        obj["days"] = s->days;
        obj["startMonth"] = s->startMonth;
        obj["startDay"] = s->startDay;
        obj["endMonth"] = s->endMonth;
        obj["endDay"] = s->endDay;
        obj["duration"] = s->duration;
        obj["enabled"] = s->enabled;
        obj["scheduleType"] = static_cast<uint8_t>(s->scheduleType);
        obj["sunOffset"] = s->sunOffset;
    }

    String result;
    serializeJson(doc, result);
    return result;
}

bool Storage::setSchedulesFromJson(const String& json) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        DEBUG_PRINTF("Storage: Failed to parse JSON: %s\n", error.c_str());
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (!arr) {
        DEBUG_PRINTLN("Storage: JSON is not an array");
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
    char timeStr[12];

    // Convert UTC hour to local time for display
    int localHour = hour + (tzOffset / 60);
    localHour = (localHour + 24) % 24;

    // Use lowercase am/pm for schedule names
    FormattedTime ft(localHour);
    const char* ampmLower = (ft.ampm[0] == 'A') ? "am" : "pm";
    snprintf(timeStr, sizeof(timeStr), "%d:%02d%s", ft.displayHour, minute, ampmLower);

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

    Settings& settings = getSettings();
    int16_t tzOffset = settings.timezoneOffset;

    // Get current UTC time and convert to local time using system timezone
    // This handles DST automatically since applyTimezone() was called
    time_t utcNow = time(nullptr);
    struct tm localTm;
    localtime_r(&utcNow, &localTm);

    int currentDay = localTm.tm_wday;  // 0 = Sunday
    int currentHour = localTm.tm_hour;
    int currentMin = localTm.tm_min;
    int currentMonth = localTm.tm_mon + 1;
    int currentDayOfMonth = localTm.tm_mday;
    int currentYear = localTm.tm_year + 1900;

    // Create DateTime for sunrise/sunset calculations
    time_t localTime = mktime(&localTm);
    DateTime localNow = DateTime((uint32_t)localTime);

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

            // Calculate the actual run time for this schedule
            int schedHour, schedMinute;

            if (s->scheduleType == ScheduleType::SPECIFIC_TIME) {
                // Use stored time directly (already in local time)
                schedHour = s->hour;
                schedMinute = s->minute;
            } else if (settings.locationSet) {
                // Calculate sunrise/sunset for the target day
                // For days in the future, add days to current local time
                DateTime futureDate = DateTime(localNow.unixtime() + (d * 86400));

                int sunMinutes;
                if (s->scheduleType == ScheduleType::SUNRISE) {
                    sunMinutes = SunCalc::getSunrise(futureDate.year(),
                                                      futureDate.month(),
                                                      futureDate.day(),
                                                      settings.latitude,
                                                      settings.longitude,
                                                      tzOffset);
                } else {  // SUNSET
                    sunMinutes = SunCalc::getSunset(futureDate.year(),
                                                     futureDate.month(),
                                                     futureDate.day(),
                                                     settings.latitude,
                                                     settings.longitude,
                                                     tzOffset);
                }

                if (sunMinutes < 0) continue;  // Sun doesn't rise/set

                // Apply offset
                sunMinutes += s->sunOffset;
                while (sunMinutes < 0) sunMinutes += 1440;
                while (sunMinutes >= 1440) sunMinutes -= 1440;

                schedHour = sunMinutes / 60;
                schedMinute = sunMinutes % 60;
            } else {
                continue;  // Can't calculate without location
            }

            // Check if this schedule time is still upcoming
            bool isToday = (d == 0);
            bool isPast = isToday && (schedHour < currentHour ||
                         (schedHour == currentHour && schedMinute <= currentMin));

            if (isPast) continue;

            // This is a valid upcoming run
            if (d < bestDaysAway ||
                (d == bestDaysAway &&
                 (schedHour < bestHour || (schedHour == bestHour && schedMinute < bestMinute)))) {
                bestDaysAway = d;
                bestHour = schedHour;
                bestMinute = schedMinute;
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
        char startTimeStr[6], endTimeStr[6];
        formatTime24Hour(startTimeStr, sizeof(startTimeStr), s->startHour, s->startMinute);
        formatTime24Hour(endTimeStr, sizeof(endTimeStr), s->endHour, s->endMinute);
        obj["startTime"] = startTimeStr;
        obj["endTime"] = endTimeStr;
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
    DEBUG_PRINTLN("Storage: Resetting to defaults");

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

    DEBUG_PRINTLN("Storage: Reset complete");
}

// =============================================================================
// Feed History
// =============================================================================

void Storage::loadFeedHistory() {
    File file = LittleFS.open(FEED_HISTORY_FILE, "r");
    if (!file) {
        DEBUG_PRINTLN("Storage: No feed history file found");
        feedHistoryCount = 0;
        feedHistoryHead = 0;
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        DEBUG_PRINTF("Storage: Failed to parse feed history: %s\n", error.c_str());
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

    // Count events first
    int eventCount = 0;
    for (JsonObject obj : arr) {
        eventCount++;
        if (eventCount >= MAX_FEED_HISTORY) break;
    }

    feedHistoryCount = eventCount;
    feedHistoryHead = (eventCount > 0) ? eventCount - 1 : 0;

    // Load events in reverse order: file has newest first, but circular buffer
    // expects oldest at low indices, newest at head (high index)
    int loadIndex = eventCount - 1;
    for (JsonObject obj : arr) {
        if (loadIndex < 0) break;

        FeedEvent& event = feedHistory[loadIndex];
        event.timestamp = obj["timestamp"] | 0;
        event.duration = obj["duration"] | 0;
        event.manual = obj["manual"] | false;
        event.status = static_cast<FeedStatus>(obj["status"] | 0);  // Default to EXECUTED
        strlcpy(event.scheduleName, obj["scheduleName"] | "", sizeof(event.scheduleName));

        loadIndex--;
    }

    DEBUG_PRINTF("Storage: Loaded %d feed history entries\n", feedHistoryCount);
}

void Storage::saveFeedHistory() {
    File file = LittleFS.open(FEED_HISTORY_FILE, "w");
    if (!file) {
        DEBUG_PRINTLN("Storage: Failed to open feed history file for writing");
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
        obj["status"] = static_cast<uint8_t>(event->status);
    }

    serializeJson(doc, file);
    file.close();

    DEBUG_PRINTF("Storage: Saved %d feed history entries\n", feedHistoryCount);
}

void Storage::logFeedEvent(uint8_t duration, bool manual, const char* scheduleName, FeedStatus status) {
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
    event.status = status;
    if (scheduleName && scheduleName[0] != '\0') {
        strlcpy(event.scheduleName, scheduleName, sizeof(event.scheduleName));
    } else {
        event.scheduleName[0] = '\0';
    }

    if (feedHistoryCount < MAX_FEED_HISTORY) {
        feedHistoryCount++;
    }

    const char* statusStr = "executed";
    if (status == FeedStatus::SKIPPED_BATTERY) statusStr = "skipped (low battery)";
    else if (status == FeedStatus::SKIPPED_RUNNING) statusStr = "skipped (motor running)";
    else if (status == FeedStatus::SKIPPED_RECENT) statusStr = "skipped (recent feed)";

    DEBUG_PRINTF("Storage: Logged feed event (duration=%ds, manual=%s, schedule=%s, status=%s)\n",
                  duration, manual ? "yes" : "no", scheduleName ? scheduleName : "", statusStr);

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
        obj["status"] = static_cast<uint8_t>(event->status);
    }

    String result;
    serializeJson(doc, result);
    return result;
}

// =============================================================================
// PIN Authentication
// =============================================================================

// Helper: compute SHA-256 hash of PIN + deviceId as salt
static void computePinHash(const char* pin, const char* salt, uint8_t* hashOut) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);  // 0 = SHA-256 (not SHA-224)
    mbedtls_sha256_update(&ctx, (const uint8_t*)pin, strlen(pin));
    mbedtls_sha256_update(&ctx, (const uint8_t*)salt, strlen(salt));
    mbedtls_sha256_finish(&ctx, hashOut);
    mbedtls_sha256_free(&ctx);
}

bool Storage::isPinSet() {
    return prefs.getBool(PREF_PIN_SET, false);
}

bool Storage::setPin(const char* pin) {
    // Validate PIN length
    size_t len = strlen(pin);
    if (len < PIN_MIN_LENGTH || len > PIN_MAX_LENGTH) {
        DEBUG_PRINTF("Storage: Invalid PIN length %d (must be %d-%d)\n",
                      len, PIN_MIN_LENGTH, PIN_MAX_LENGTH);
        return false;
    }

    // Validate all digits
    for (size_t i = 0; i < len; i++) {
        if (!isdigit(pin[i])) {
            DEBUG_PRINTLN("Storage: PIN must contain only digits");
            return false;
        }
    }

    // Compute hash with deviceId as salt
    uint8_t hash[32];
    computePinHash(pin, deviceId, hash);

    // Store hash as base64 string (shorter than hex)
    char hashStr[45];  // Base64 of 32 bytes = 44 chars + null
    size_t outLen;
    mbedtls_base64_encode((uint8_t*)hashStr, sizeof(hashStr), &outLen, hash, 32);
    hashStr[outLen] = '\0';

    prefs.putString(PREF_PIN_HASH, hashStr);
    prefs.putBool(PREF_PIN_SET, true);
    resetFailedPinAttempts();

    DEBUG_PRINTLN("Storage: PIN set successfully");
    return true;
}

bool Storage::verifyPin(const char* pin) {
    if (!isPinSet()) {
        // No PIN set = always valid (unlocked device)
        return true;
    }

    // Check lockout
    uint32_t lockoutEnd = getLockoutEndTime();
    if (lockoutEnd > 0 && millis() < lockoutEnd) {
        DEBUG_PRINTLN("Storage: PIN verification blocked - device locked out");
        return false;
    }

    // Compute hash of provided PIN
    uint8_t hash[32];
    computePinHash(pin, deviceId, hash);

    // Convert to base64 for comparison
    char hashStr[45];
    size_t outLen;
    mbedtls_base64_encode((uint8_t*)hashStr, sizeof(hashStr), &outLen, hash, 32);
    hashStr[outLen] = '\0';

    // Compare with stored hash
    String storedHash = prefs.getString(PREF_PIN_HASH, "");
    if (storedHash == hashStr) {
        resetFailedPinAttempts();
        DEBUG_PRINTLN("Storage: PIN verified successfully");
        return true;
    }

    // Wrong PIN - increment failed attempts
    incrementFailedPinAttempts();
    DEBUG_PRINTF("Storage: Wrong PIN (%d failed attempts)\n", getFailedPinAttempts());
    return false;
}

void Storage::clearPin() {
    prefs.remove(PREF_PIN_HASH);
    prefs.putBool(PREF_PIN_SET, false);
    resetFailedPinAttempts();
    DEBUG_PRINTLN("Storage: PIN cleared");
}

uint8_t Storage::getFailedPinAttempts() {
    return prefs.getUChar(PREF_PIN_FAILS, 0);
}

void Storage::incrementFailedPinAttempts() {
    uint8_t attempts = getFailedPinAttempts() + 1;
    prefs.putUChar(PREF_PIN_FAILS, attempts);

    // Apply lockout if max attempts reached
    if (attempts >= PIN_MAX_ATTEMPTS) {
        setLockout(PIN_LOCKOUT_DURATION_MS);
    }
}

void Storage::resetFailedPinAttempts() {
    prefs.putUChar(PREF_PIN_FAILS, 0);
    prefs.putULong(PREF_PIN_LOCKOUT, 0);
}

uint32_t Storage::getLockoutEndTime() {
    return prefs.getULong(PREF_PIN_LOCKOUT, 0);
}

void Storage::setLockout(uint32_t durationMs) {
    uint32_t endTime = millis() + durationMs;
    prefs.putULong(PREF_PIN_LOCKOUT, endTime);
    DEBUG_PRINTF("Storage: Device locked out for %d seconds\n", durationMs / 1000);
}

// =============================================================================
// Paired Device Management (for BLE pairing security)
// =============================================================================

bool Storage::isPairedDevice(const char* bleAddress) {
    if (!bleAddress || strlen(bleAddress) == 0) {
        return false;
    }

    int count = getPairedDeviceCount();
    for (int i = 0; i < count; i++) {
        char key[12];
        snprintf(key, sizeof(key), "%s%d", PREF_PAIRED_PREFIX, i);
        String stored = prefs.getString(key, "");
        if (stored.equalsIgnoreCase(bleAddress)) {
            return true;
        }
    }
    return false;
}

bool Storage::addPairedDevice(const char* bleAddress) {
    if (!bleAddress || strlen(bleAddress) == 0) {
        return false;
    }

    // Check if already paired
    if (isPairedDevice(bleAddress)) {
        DEBUG_PRINTF("Storage: Device %s already paired\n", bleAddress);
        return true;
    }

    int count = getPairedDeviceCount();

    // Check limit
    if (count >= MAX_PAIRED_DEVICES) {
        // Remove oldest device to make room
        DEBUG_PRINTLN("Storage: Paired device limit reached, removing oldest");
        for (int i = 0; i < count - 1; i++) {
            char keyOld[12], keyNew[12];
            snprintf(keyOld, sizeof(keyOld), "%s%d", PREF_PAIRED_PREFIX, i + 1);
            snprintf(keyNew, sizeof(keyNew), "%s%d", PREF_PAIRED_PREFIX, i);
            String addr = prefs.getString(keyOld, "");
            prefs.putString(keyNew, addr.c_str());
        }
        count = MAX_PAIRED_DEVICES - 1;
    }

    // Add new device
    char key[12];
    snprintf(key, sizeof(key), "%s%d", PREF_PAIRED_PREFIX, count);
    prefs.putString(key, bleAddress);
    prefs.putUChar(PREF_PAIRED_COUNT, count + 1);

    DEBUG_PRINTF("Storage: Paired device added: %s (total: %d)\n", bleAddress, count + 1);
    return true;
}

bool Storage::removePairedDevice(const char* bleAddress) {
    if (!bleAddress || strlen(bleAddress) == 0) {
        return false;
    }

    int count = getPairedDeviceCount();
    int foundIndex = -1;

    // Find the device
    for (int i = 0; i < count; i++) {
        char key[12];
        snprintf(key, sizeof(key), "%s%d", PREF_PAIRED_PREFIX, i);
        String stored = prefs.getString(key, "");
        if (stored.equalsIgnoreCase(bleAddress)) {
            foundIndex = i;
            break;
        }
    }

    if (foundIndex < 0) {
        return false;
    }

    // Shift remaining devices down
    for (int i = foundIndex; i < count - 1; i++) {
        char keyOld[12], keyNew[12];
        snprintf(keyOld, sizeof(keyOld), "%s%d", PREF_PAIRED_PREFIX, i + 1);
        snprintf(keyNew, sizeof(keyNew), "%s%d", PREF_PAIRED_PREFIX, i);
        String addr = prefs.getString(keyOld, "");
        prefs.putString(keyNew, addr.c_str());
    }

    // Remove last slot
    char lastKey[12];
    snprintf(lastKey, sizeof(lastKey), "%s%d", PREF_PAIRED_PREFIX, count - 1);
    prefs.remove(lastKey);
    prefs.putUChar(PREF_PAIRED_COUNT, count - 1);

    DEBUG_PRINTF("Storage: Paired device removed: %s\n", bleAddress);
    return true;
}

void Storage::clearAllPairedDevices() {
    int count = getPairedDeviceCount();
    for (int i = 0; i < count; i++) {
        char key[12];
        snprintf(key, sizeof(key), "%s%d", PREF_PAIRED_PREFIX, i);
        prefs.remove(key);
    }
    prefs.putUChar(PREF_PAIRED_COUNT, 0);
    DEBUG_PRINTLN("Storage: All paired devices cleared");
}

int Storage::getPairedDeviceCount() {
    return prefs.getUChar(PREF_PAIRED_COUNT, 0);
}
