#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>

class RTCManager {
public:
    bool begin();

    // Time access
    DateTime now();
    void setTime(uint32_t unixTime);
    void setTime(const DateTime& dt);

    // Sync status - tracks whether time has been synchronized
    bool isTimeSynced() const { return timeSynced; }
    void setTimeSynced(bool synced);

    // Check if RTC lost power (battery died or first boot)
    bool lostPower();

    // Check if RTC is available
    bool isAvailable() const { return rtcAvailable; }

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
    RTC_DS3231 rtc;
    bool initialized = false;
    bool rtcAvailable = false;
    bool timeSynced = false;

    void loadSyncStatus();
    void saveSyncStatus();
    void syncSystemTime();  // Sync ESP32 system time from DS3231
};

extern RTCManager rtcManager;
