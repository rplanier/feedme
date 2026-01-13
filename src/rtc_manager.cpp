#include "rtc_manager.h"
#include "config.h"
#include <Preferences.h>
#include <sys/time.h>

RTCManager rtcManager;

// =============================================================================
// DateTime Implementation
// =============================================================================

DateTime::DateTime(uint16_t year, uint8_t month, uint8_t day,
                   uint8_t hour, uint8_t minute, uint8_t second) {
    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    unixTime = mktime(&t);
}

uint16_t DateTime::year() const {
    time_t t = unixTime;
    struct tm* tm = localtime(&t);
    return tm->tm_year + 1900;
}

uint8_t DateTime::month() const {
    time_t t = unixTime;
    struct tm* tm = localtime(&t);
    return tm->tm_mon + 1;
}

uint8_t DateTime::day() const {
    time_t t = unixTime;
    struct tm* tm = localtime(&t);
    return tm->tm_mday;
}

uint8_t DateTime::hour() const {
    time_t t = unixTime;
    struct tm* tm = localtime(&t);
    return tm->tm_hour;
}

uint8_t DateTime::minute() const {
    time_t t = unixTime;
    struct tm* tm = localtime(&t);
    return tm->tm_min;
}

uint8_t DateTime::second() const {
    time_t t = unixTime;
    struct tm* tm = localtime(&t);
    return tm->tm_sec;
}

uint8_t DateTime::dayOfTheWeek() const {
    time_t t = unixTime;
    struct tm* tm = localtime(&t);
    return tm->tm_wday;  // 0 = Sunday
}

// =============================================================================
// RTCManager Implementation
// =============================================================================

void RTCManager::begin() {
    // Load sync status from preferences
    loadSyncStatus();

    // If time is clearly invalid (before 2024), mark as not synced
    DateTime current = now();
    if (current.year() < 2024) {
        Serial.println("RTC: Time appears invalid (year < 2024)");
        setTimeSynced(false);
    }

    initialized = true;
    Serial.printf("RTC: Initialized (internal), synced=%d, time=%04d-%02d-%02d %02d:%02d:%02d\n",
                  timeSynced, current.year(), current.month(), current.day(),
                  current.hour(), current.minute(), current.second());
}

DateTime RTCManager::now() {
    time_t t;
    time(&t);
    return DateTime(t);
}

void RTCManager::setTime(uint32_t unixTime) {
    struct timeval tv;
    tv.tv_sec = unixTime;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);

    setTimeSynced(true);

    DateTime dt(unixTime);
    Serial.printf("RTC: Time set to %04d-%02d-%02d %02d:%02d:%02d\n",
                  dt.year(), dt.month(), dt.day(),
                  dt.hour(), dt.minute(), dt.second());
}

void RTCManager::setTime(const DateTime& dt) {
    setTime(dt.unixtime());
}

bool RTCManager::lostPower() const {
    // Internal RTC always loses time on power loss
    // This will return true after reboot if time wasn't synced
    return !timeSynced;
}

void RTCManager::setTimeSynced(bool synced) {
    if (timeSynced != synced) {
        timeSynced = synced;
        saveSyncStatus();
        Serial.printf("RTC: Sync status changed to %d\n", synced);
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
    // Local = UTC - offset (offset is positive for behind UTC)
    int32_t localUnix = utc.unixtime() - (tzOffset * 60);
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
