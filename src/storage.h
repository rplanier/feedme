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
    uint8_t hour;                   // 0-23 (stored as UTC) - used for SPECIFIC_TIME
    uint8_t minute;                 // 0-59 - used for SPECIFIC_TIME
    uint8_t days;                   // Bitmask: bit 0 = Sun, bit 1 = Mon, ... bit 6 = Sat
    int16_t startMonth;             // 1-12, or -1 for no start date
    int16_t startDay;               // 1-31
    int16_t endMonth;               // 1-12, or -1 for no end date
    int16_t endDay;                 // 1-31
    uint8_t duration;               // Override duration (0 = use default)
    bool enabled;
    ScheduleType scheduleType;      // SPECIFIC_TIME, SUNRISE, or SUNSET
    int16_t sunOffset;              // Minutes offset from sunrise/sunset (-120 to +120)

    // Helper methods
    bool isActiveOnDay(uint8_t dayOfWeek) const;  // 0 = Sunday
    bool isActiveOnDate(int month, int day) const;
    void generateName(int16_t tzOffset);  // Auto-generate name from schedule properties (uses local time)

    // UTC/Local time conversion helpers
    // tzOffset is minutes from UTC (negative = west of UTC, like Swift's secondsFromGMT)
    uint8_t getLocalHour(int16_t tzOffset) const {
        // Convert UTC hour to local: local = UTC + offset/60
        int localHour = hour + (tzOffset / 60);
        return (localHour + 24) % 24;
    }

    static uint8_t toUtcHour(uint8_t localHour, int16_t tzOffset) {
        // Convert local hour to UTC: UTC = local - offset/60
        int utcHour = localHour - (tzOffset / 60);
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

    // UTC/Local time conversion helpers (tzOffset negative = west of UTC)
    uint8_t getLocalStartHour(int16_t tzOffset) const {
        int localHour = startHour + (tzOffset / 60);
        return (localHour + 24) % 24;
    }

    uint8_t getLocalEndHour(int16_t tzOffset) const {
        int localHour = endHour + (tzOffset / 60);
        return (localHour + 24) % 24;
    }

    static uint8_t toUtcHour(uint8_t localHour, int16_t tzOffset) {
        int utcHour = localHour - (tzOffset / 60);
        return (utcHour + 24) % 24;
    }
};

// Feed event history entry
enum class FeedStatus : uint8_t {
    EXECUTED = 0,       // Feed ran successfully
    SKIPPED_BATTERY = 1, // Skipped due to low battery
    SKIPPED_RUNNING = 2, // Skipped because motor already running
    SKIPPED_RECENT = 3   // Skipped due to recent feed (overlap protection)
};

struct FeedEvent {
    uint32_t timestamp;             // Unix epoch time
    uint8_t duration;               // Duration in seconds (0 if skipped)
    bool manual;                    // true = Feed Now/display, false = scheduled
    FeedStatus status;              // Execution status
    char scheduleName[24];          // Schedule name (empty if manual)
};

constexpr int MAX_FEED_HISTORY = 20;

// Settings structure
struct Settings {
    char deviceId[5];               // 4-digit ID + null
    char deviceName[33];            // User-defined name (max 32 chars + null), empty = use default "FeedMe-XXXX"
    uint8_t motorDuration;          // Default motor duration in seconds
    bool vacationMode;
    SleepTimeout sleepTimeout;      // Display/sleep timeout
    int16_t timezoneOffset;         // Minutes from UTC (negative = west of UTC) - DEPRECATED, use posixTz
    char posixTz[48];               // POSIX timezone string (e.g., "CST6CDT,M3.2.0,M11.1.0") for DST handling
    BatteryType batteryType;        // Battery chemistry (SLA/AGM/GEL) for accurate state-of-charge
    AntennaType antennaType;        // Antenna selection (rod/onboard)
    float latitude;                 // GPS latitude for sunrise/sunset calculation
    float longitude;                // GPS longitude for sunrise/sunset calculation
    bool locationSet;               // True if location has been set by user
    uint8_t inactivityTimeoutMin;   // Inactivity timeout in minutes (3-60, 0 = never)

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

    // Get inactivity timeout in milliseconds (0 = never)
    uint32_t getInactivityTimeoutMs() const {
        if (inactivityTimeoutMin == INACTIVITY_NEVER) return 0;
        return (uint32_t)inactivityTimeoutMin * 60 * 1000;
    }
};

// Pairing/authentication constants
constexpr uint8_t PIN_LENGTH = 4;                     // Generated pairing PIN is always 4 digits
constexpr uint8_t PIN_MAX_ATTEMPTS = 5;
constexpr uint32_t PIN_LOCKOUT_DURATION_MS = 60000;   // 1 minute lockout after max attempts
constexpr uint8_t MAX_PAIRED_DEVICES = 8;             // Max number of remembered paired devices

// Legacy constants (deprecated)
constexpr uint8_t PIN_MIN_LENGTH = 4;
constexpr uint8_t PIN_MAX_LENGTH = 6;

class Storage {
public:
    bool begin();

    // Device ID (generated once on first boot)
    const char* getDeviceId();

    // Settings
    Settings& getSettings() { return settings; }
    void saveSettings();
    void loadSettings();
    void applyTimezone();  // Apply POSIX timezone to system (enables localtime() DST handling)

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
    void logFeedEvent(uint8_t duration, bool manual, const char* scheduleName, FeedStatus status = FeedStatus::EXECUTED);
    int getFeedHistoryCount() const { return feedHistoryCount; }
    const FeedEvent* getFeedEvent(int index) const;  // 0 = most recent
    String getFeedHistoryJson();

    // Reset all settings and schedules to defaults
    void resetToDefaults();

    // Paired device management (for BLE security)
    bool isPairedDevice(const char* bleAddress);     // Check if device is in paired list
    bool addPairedDevice(const char* bleAddress);    // Add device to paired list (max 8)
    bool removePairedDevice(const char* bleAddress); // Remove device from paired list
    void clearAllPairedDevices();                    // Remove all paired devices
    int getPairedDeviceCount();                      // Number of paired devices

    // PIN lockout (for failed pairing attempts)
    uint8_t getFailedPinAttempts();
    void incrementFailedPinAttempts();
    void resetFailedPinAttempts();
    uint32_t getLockoutEndTime();                    // Returns 0 if not locked out
    void setLockout(uint32_t durationMs);

    // Legacy PIN functions (deprecated - kept for migration)
    bool isPinSet();
    bool setPin(const char* pin);
    bool verifyPin(const char* pin);
    void clearPin();

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
