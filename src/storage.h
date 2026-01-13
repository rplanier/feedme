#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "schedule_manager.h"

// Schedule structure
struct Schedule {
    uint16_t id;                    // Unique ID
    char name[48];                  // Display name (auto-generated or custom)
    uint8_t hour;                   // 0-23 (stored as UTC)
    uint8_t minute;                 // 0-59
    uint8_t days;                   // Bitmask: bit 0 = Sun, bit 1 = Mon, ... bit 6 = Sat
    int16_t startMonth;             // 1-12, or -1 for no start date
    int16_t startDay;               // 1-31
    int16_t endMonth;               // 1-12, or -1 for no end date
    int16_t endDay;                 // 1-31
    uint8_t duration;               // Override duration (0 = use default)
    bool enabled;

    // Helper methods
    bool isActiveOnDay(uint8_t dayOfWeek) const;  // 0 = Sunday
    bool isActiveOnDate(int month, int day) const;
    void generateName(int16_t tzOffset);  // Auto-generate name from schedule properties (uses local time)

    // UTC/Local time conversion helpers
    // tzOffset is minutes from UTC (positive = behind UTC, like JS getTimezoneOffset)
    uint8_t getLocalHour(int16_t tzOffset) const {
        // Convert UTC hour to local: local = UTC - offset/60
        int localHour = hour - (tzOffset / 60);
        return (localHour + 24) % 24;
    }

    static uint8_t toUtcHour(uint8_t localHour, int16_t tzOffset) {
        // Convert local hour to UTC: UTC = local + offset/60
        int utcHour = localHour + (tzOffset / 60);
        return (utcHour + 24) % 24;
    }
};

// BLE Schedule structure (time windows when BLE advertising is active)
struct BleSchedule {
    uint16_t id;                    // Unique ID
    char name[32];                  // Display name
    uint8_t startHour;              // Start time hour (0-23, stored as UTC)
    uint8_t startMinute;            // Start time minute (0-59)
    uint8_t endHour;                // End time hour (0-23, stored as UTC)
    uint8_t endMinute;              // End time minute (0-59)
    uint8_t days;                   // Bitmask: bit 0 = Sun, bit 1 = Mon, ... bit 6 = Sat
    bool enabled;

    // Helper methods
    bool isActiveOnDay(uint8_t dayOfWeek) const;  // 0 = Sunday
    bool isActiveNow(int hour, int minute, int dayOfWeek) const;

    // UTC/Local time conversion helpers
    uint8_t getLocalStartHour(int16_t tzOffset) const {
        int localHour = startHour - (tzOffset / 60);
        return (localHour + 24) % 24;
    }

    uint8_t getLocalEndHour(int16_t tzOffset) const {
        int localHour = endHour - (tzOffset / 60);
        return (localHour + 24) % 24;
    }

    static uint8_t toUtcHour(uint8_t localHour, int16_t tzOffset) {
        int utcHour = localHour + (tzOffset / 60);
        return (utcHour + 24) % 24;
    }
};

// Feed event history entry
struct FeedEvent {
    uint32_t timestamp;             // Unix epoch time
    uint8_t duration;               // Duration in seconds
    bool manual;                    // true = Feed Now/display, false = scheduled
    char scheduleName[24];          // Schedule name (empty if manual)
};

constexpr int MAX_FEED_HISTORY = 20;

// Settings structure
struct Settings {
    char deviceId[5];               // 4-digit ID + null
    uint8_t motorDuration;          // Default motor duration in seconds
    bool vacationMode;
    SleepTimeout sleepTimeout;      // Display/sleep timeout
    int16_t timezoneOffset;         // Minutes from UTC (positive = behind UTC, like JS getTimezoneOffset)
    BatteryType batteryType;        // Battery chemistry (SLA/AGM/GEL) for accurate state-of-charge

    uint16_t getSleepTimeoutSeconds() const {
        return ::getSleepTimeoutSeconds(sleepTimeout);
    }

    const char* getSleepTimeoutName() const {
        switch (sleepTimeout) {
            case SleepTimeout::TIMEOUT_15S: return "15 sec";
            case SleepTimeout::TIMEOUT_30S: return "30 sec";
            case SleepTimeout::TIMEOUT_1M: return "1 min";
            case SleepTimeout::TIMEOUT_2M: return "2 min";
            case SleepTimeout::TIMEOUT_5M: return "5 min";
            case SleepTimeout::TIMEOUT_NEVER: return "Never";
            default: return "30 sec";
        }
    }
};

class Storage {
public:
    bool begin();

    // Device ID (generated once on first boot)
    const char* getDeviceId();

    // Settings
    Settings& getSettings() { return settings; }
    void saveSettings();
    void loadSettings();

    // Schedules
    int getScheduleCount() const { return feedSchedules.getCount(); }
    Schedule* getSchedule(int index);
    Schedule* getScheduleById(uint16_t id);
    bool addSchedule(const Schedule& schedule);
    bool updateSchedule(uint16_t id, const Schedule& schedule);
    bool deleteSchedule(uint16_t id);
    void saveSchedules();
    void loadSchedules();

    // Get all schedules as JSON
    String getSchedulesJson();
    bool setSchedulesFromJson(const String& json);

    // Get next scheduled run time (returns false if none)
    // daysAway: 0=today, 1=tomorrow, 2-6=day of week
    bool getNextRunTime(int& hour, int& minute, int& daysAway);

    // BLE Schedules
    int getBleScheduleCount() const { return bleSchedules.getCount(); }
    BleSchedule* getBleSchedule(int index);
    BleSchedule* getBleScheduleById(uint16_t id);
    bool addBleSchedule(const BleSchedule& schedule);
    bool updateBleSchedule(uint16_t id, const BleSchedule& schedule);
    bool deleteBleSchedule(uint16_t id);
    void saveBleSchedules();
    void loadBleSchedules();
    String getBleSchedulesJson();

    // Check if BLE should be active now based on schedules
    bool shouldBleBeActive();

    // Feed history
    void logFeedEvent(uint8_t duration, bool manual, const char* scheduleName);
    int getFeedHistoryCount() const { return feedHistoryCount; }
    const FeedEvent* getFeedEvent(int index) const;  // 0 = most recent
    String getFeedHistoryJson();

    // Reset all settings and schedules to defaults
    void resetToDefaults();

private:
    Preferences prefs;
    Settings settings;
    char deviceId[5] = "";

    static constexpr int MAX_SCHEDULES = 32;
    static constexpr int MAX_BLE_SCHEDULES = 8;

    ScheduleManager<Schedule, MAX_SCHEDULES> feedSchedules;
    ScheduleManager<BleSchedule, MAX_BLE_SCHEDULES> bleSchedules;

    // Feed history (circular buffer, most recent first)
    FeedEvent feedHistory[MAX_FEED_HISTORY];
    int feedHistoryCount = 0;
    int feedHistoryHead = 0;  // Index of most recent event

    void generateDeviceId();
    void loadFeedHistory();
    void saveFeedHistory();
    void initDefaultSettings();

    // Serialization helpers for ScheduleManager
    static void deserializeFeedSchedule(Schedule& s, JsonObject& obj);
    static void serializeFeedSchedule(const Schedule& s, JsonObject& obj);
    static void deserializeBleSchedule(BleSchedule& s, JsonObject& obj);
    static void serializeBleSchedule(const BleSchedule& s, JsonObject& obj);
};

extern Storage storage;
