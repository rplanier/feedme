#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"

// Schedule structure
struct Schedule {
    uint16_t id;                    // Unique ID
    char name[48];                  // Display name (auto-generated or custom)
    uint8_t hour;                   // 0-23
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
    void generateName();            // Auto-generate name from schedule properties
};

// WiFi Schedule structure (time windows when WiFi is available)
struct WifiSchedule {
    uint16_t id;                    // Unique ID
    char name[32];                  // Display name
    uint8_t startHour;              // Start time hour (0-23)
    uint8_t startMinute;            // Start time minute (0-59)
    uint8_t endHour;                // End time hour (0-23)
    uint8_t endMinute;              // End time minute (0-59)
    uint8_t days;                   // Bitmask: bit 0 = Sun, bit 1 = Mon, ... bit 6 = Sat
    bool enabled;

    // Helper methods
    bool isActiveOnDay(uint8_t dayOfWeek) const;  // 0 = Sunday
    bool isActiveNow(int hour, int minute, int dayOfWeek) const;
};

// Settings structure
struct Settings {
    char deviceId[5];               // 4-digit ID + null
    uint8_t motorDuration;          // Default motor duration in seconds
    bool vacationMode;
    BatteryType batteryType;        // SLA, AGM, or GEL

    // Get critical voltage threshold based on battery type
    float getCriticalVoltage() const {
        switch (batteryType) {
            case BatteryType::AGM: return BATTERY_CRITICAL_AGM;
            case BatteryType::GEL: return BATTERY_CRITICAL_GEL;
            default: return BATTERY_CRITICAL_SLA;
        }
    }

    const char* getBatteryTypeName() const {
        switch (batteryType) {
            case BatteryType::AGM: return "AGM";
            case BatteryType::GEL: return "Gel";
            default: return "SLA";
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
    int getScheduleCount() const { return scheduleCount; }
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

    // WiFi Schedules
    int getWifiScheduleCount() const { return wifiScheduleCount; }
    WifiSchedule* getWifiSchedule(int index);
    WifiSchedule* getWifiScheduleById(uint16_t id);
    bool addWifiSchedule(const WifiSchedule& schedule);
    bool updateWifiSchedule(uint16_t id, const WifiSchedule& schedule);
    bool deleteWifiSchedule(uint16_t id);
    void saveWifiSchedules();
    void loadWifiSchedules();
    String getWifiSchedulesJson();

    // Check if WiFi should be active now based on schedules
    bool shouldWifiBeActive();

    // Reset all settings and schedules to defaults
    void resetToDefaults();

private:
    Preferences prefs;
    Settings settings;
    char deviceId[5] = "";

    static constexpr int MAX_SCHEDULES = 32;
    Schedule schedules[MAX_SCHEDULES];
    int scheduleCount = 0;
    uint16_t nextScheduleId = 1;

    static constexpr int MAX_WIFI_SCHEDULES = 8;
    WifiSchedule wifiSchedules[MAX_WIFI_SCHEDULES];
    int wifiScheduleCount = 0;
    uint16_t nextWifiScheduleId = 1;

    void generateDeviceId();
    void initDefaultSettings();
};

extern Storage storage;
