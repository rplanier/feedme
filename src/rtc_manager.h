#pragma once

#include <Arduino.h>
#include <time.h>

// Simple DateTime class for compatibility with RTClib when we switch to DS3231
class DateTime {
public:
    DateTime() : unixTime(0) {}
    DateTime(uint32_t t) : unixTime(t) {}
    DateTime(uint16_t year, uint8_t month, uint8_t day,
             uint8_t hour = 0, uint8_t minute = 0, uint8_t second = 0);

    uint16_t year() const;
    uint8_t month() const;
    uint8_t day() const;
    uint8_t hour() const;
    uint8_t minute() const;
    uint8_t second() const;
    uint8_t dayOfTheWeek() const;  // 0 = Sunday

    uint32_t unixtime() const { return unixTime; }

    // Comparison operators
    bool operator<(const DateTime& other) const { return unixTime < other.unixTime; }
    bool operator>(const DateTime& other) const { return unixTime > other.unixTime; }
    bool operator<=(const DateTime& other) const { return unixTime <= other.unixTime; }
    bool operator>=(const DateTime& other) const { return unixTime >= other.unixTime; }
    bool operator==(const DateTime& other) const { return unixTime == other.unixTime; }

private:
    uint32_t unixTime;
};

class RTCManager {
public:
    void begin();

    // Time access
    DateTime now();
    void setTime(uint32_t unixTime);
    void setTime(const DateTime& dt);

    // Sync status - tracks whether time has been synchronized
    bool isTimeSynced() const { return timeSynced; }
    void setTimeSynced(bool synced);

    // For future DS3231: check if RTC lost power
    bool lostPower() const;

    // Format helpers (UTC time from RTC)
    void formatTime(char* buffer, size_t len);       // "HH:MM"
    void formatDate(char* buffer, size_t len);       // "Mon Jan 01"
    void formatDateTime(char* buffer, size_t len);   // "Mon Jan 01 HH:MM"

    // Format helpers with timezone conversion (UTC -> Local)
    // tzOffset is minutes from UTC (negative = west of UTC, like Swift's secondsFromGMT)
    void formatTimeLocal(char* buffer, size_t len, int16_t tzOffset);
    void formatDateLocal(char* buffer, size_t len, int16_t tzOffset);
    void formatDateTimeLocal(char* buffer, size_t len, int16_t tzOffset);

    // Get local DateTime (applies timezone offset)
    DateTime nowLocal(int16_t tzOffset);

private:
    bool initialized = false;
    bool timeSynced = false;

    void loadSyncStatus();
    void saveSyncStatus();
};

extern RTCManager rtcManager;
