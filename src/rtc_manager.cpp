#include "rtc_manager.h"
#include "config.h"
#include <Preferences.h>
#include <sys/time.h>

RTCManager rtcManager;

// =============================================================================
// RTCManager Implementation - DS3231M RTC
// =============================================================================

bool RTCManager::begin() {
    // Initialize I2C with correct pins for DS3231M
    Wire.begin(PIN_RTC_SDA, PIN_RTC_SCL);

    // Try to initialize DS3231
    if (!rtc.begin(&Wire)) {
        DEBUG_PRINTLN("RTC: DS3231M not found!");
        rtcAvailable = false;
        initialized = true;
        return false;
    }

    rtcAvailable = true;
    DEBUG_PRINTLN("RTC: DS3231M initialized");

    // Check if RTC lost power (battery died or first use)
    if (rtc.lostPower()) {
        DEBUG_PRINTLN("RTC: DS3231M lost power, time is invalid");
        setTimeSynced(false);
    } else {
        // RTC has valid time, load sync status from preferences
        loadSyncStatus();

        // Sync ESP32 system time from DS3231
        syncSystemTime();
    }

    // Validate time - if year < 2024, mark as not synced
    DateTime current = now();
    if (current.year() < 2024) {
        DEBUG_PRINTLN("RTC: Time appears invalid (year < 2024)");
        setTimeSynced(false);
    }

    initialized = true;
    DEBUG_PRINTF("RTC: DS3231M ready, synced=%d, time=%04d-%02d-%02d %02d:%02d:%02d\n",
                  timeSynced, current.year(), current.month(), current.day(),
                  current.hour(), current.minute(), current.second());

    return true;
}

DateTime RTCManager::now() {
    if (rtcAvailable) {
        return rtc.now();
    }
    // Fallback to system time if DS3231 not available
    time_t t;
    time(&t);
    return DateTime(t);
}

void RTCManager::setTime(uint32_t unixTime) {
    DateTime dt(unixTime);

    if (rtcAvailable) {
        // Set DS3231M time
        rtc.adjust(dt);
        DEBUG_PRINTLN("RTC: DS3231M time updated");
    }

    // Also set ESP32 system time for compatibility
    struct timeval tv;
    tv.tv_sec = unixTime;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);

    setTimeSynced(true);

    DEBUG_PRINTF("RTC: Time set to %04d-%02d-%02d %02d:%02d:%02d\n",
                  dt.year(), dt.month(), dt.day(),
                  dt.hour(), dt.minute(), dt.second());
}

void RTCManager::setTime(const DateTime& dt) {
    setTime(dt.unixtime());
}

bool RTCManager::lostPower() {
    if (rtcAvailable) {
        return rtc.lostPower();
    }
    // If RTC not available, consider power always lost
    return true;
}

void RTCManager::setTimeSynced(bool synced) {
    if (timeSynced != synced) {
        timeSynced = synced;
        saveSyncStatus();
        DEBUG_PRINTF("RTC: Sync status changed to %d\n", synced);
    }
}

void RTCManager::loadSyncStatus() {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, true);
    timeSynced = prefs.getBool(PREF_TIME_SYNCED, false);
    prefs.end();
}

void RTCManager::saveSyncStatus() {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);
    prefs.putBool(PREF_TIME_SYNCED, timeSynced);
    prefs.end();
}

void RTCManager::syncSystemTime() {
    if (!rtcAvailable) return;

    // Read time from DS3231 and set ESP32 system time
    DateTime dt = rtc.now();
    struct timeval tv;
    tv.tv_sec = dt.unixtime();
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);

    DEBUG_PRINTF("RTC: System time synced from DS3231M: %04d-%02d-%02d %02d:%02d:%02d\n",
                  dt.year(), dt.month(), dt.day(),
                  dt.hour(), dt.minute(), dt.second());
}

void RTCManager::formatTime(char* buffer, size_t len) {
    DateTime dt = now();
    snprintf(buffer, len, "%02d:%02d", dt.hour(), dt.minute());
}

void RTCManager::formatDate(char* buffer, size_t len) {
    DateTime dt = now();
    snprintf(buffer, len, "%s %s %02d", DAY_NAMES[dt.dayOfTheWeek()],
             MONTH_NAMES[dt.month() - 1], dt.day());
}

void RTCManager::formatDateTime(char* buffer, size_t len) {
    DateTime dt = now();
    snprintf(buffer, len, "%s %s %02d %02d:%02d",
             DAY_NAMES[dt.dayOfTheWeek()], MONTH_NAMES[dt.month() - 1], dt.day(),
             dt.hour(), dt.minute());
}

// Local time formatting (with timezone offset conversion)

DateTime RTCManager::nowLocal(int16_t tzOffset) {
    DateTime utc = now();
    // Local = UTC + offset (offset is negative for west of UTC, like Swift's secondsFromGMT)
    int32_t localUnix = utc.unixtime() + (tzOffset * 60);
    return DateTime(localUnix);
}

void RTCManager::formatTimeLocal(char* buffer, size_t len, int16_t tzOffset) {
    DateTime dt = nowLocal(tzOffset);
    snprintf(buffer, len, "%02d:%02d", dt.hour(), dt.minute());
}

void RTCManager::formatDateLocal(char* buffer, size_t len, int16_t tzOffset) {
    DateTime dt = nowLocal(tzOffset);
    snprintf(buffer, len, "%s %s %02d", DAY_NAMES[dt.dayOfTheWeek()],
             MONTH_NAMES[dt.month() - 1], dt.day());
}

void RTCManager::formatDateTimeLocal(char* buffer, size_t len, int16_t tzOffset) {
    DateTime dt = nowLocal(tzOffset);
    snprintf(buffer, len, "%s %s %02d %02d:%02d",
             DAY_NAMES[dt.dayOfTheWeek()], MONTH_NAMES[dt.month() - 1], dt.day(),
             dt.hour(), dt.minute());
}
