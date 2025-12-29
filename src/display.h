#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "buttons.h"

// UI Screen states
enum class Screen {
    OVERVIEW,
    SCHEDULES,
    SCHEDULE_EDIT,
    SETTINGS,
    SETTINGS_EDIT,
    TIME_EDIT,
    DATE_EDIT,
    CONNECTIVITY,
    WIFI_SCHEDULES,
    WIFI_SCHEDULE_EDIT,
    ABOUT,
    CONFIRM_RESET,
    THROW_CONFIRM
};

// Colors
namespace Colors {
    constexpr uint16_t BACKGROUND = TFT_BLACK;
    constexpr uint16_t TEXT = TFT_WHITE;
    constexpr uint16_t TEXT_DIM = TFT_DARKGREY;
    constexpr uint16_t ACCENT = TFT_CYAN;
    constexpr uint16_t SUCCESS = TFT_GREEN;
    constexpr uint16_t WARNING = TFT_YELLOW;
    constexpr uint16_t DANGER = TFT_RED;
    constexpr uint16_t HEADER_BG = 0x1082;  // Dark blue-grey
}

// Forward declarations for data types (will be defined in other modules)
struct StatusData {
    float batteryVoltage;
    bool isCharging;
    const char* batteryStatus;  // "Good", "Okay", "Low"
    char nextFeedTime[16];      // "Today 07:00" or "Mon 07:00"
    char currentTime[6];        // "HH:MM"
    char currentDate[12];       // "Mon DD YYYY"
    bool wifiConnected;
    bool vacationMode;
};

class Display {
public:
    void begin();
    void update();
    void handleButton(ButtonEvent event);

    // Screen management
    void setScreen(Screen screen);
    Screen getScreen() const { return currentScreen; }

    // Force redraw on next update
    void invalidate() { needsRedraw = true; }

    // Backlight control
    void setBacklight(bool on);
    bool isBacklightOn() const { return backlightOn; }

    // Update status data for home screen
    void setStatus(const StatusData& status);

    // Set pairing info
    void setPairingInfo(const char* ssid, const char* password);

private:
    TFT_eSPI tft = TFT_eSPI();
    Screen currentScreen = Screen::OVERVIEW;
    Screen previousScreen = Screen::OVERVIEW;
    bool needsRedraw = true;
    bool backlightOn = true;

    // Status data cache
    StatusData status = {};
    char pairingSSID[32] = "";
    char pairingPassword[32] = "";

    // UI state
    int menuIndex = 0;
    int scrollOffset = 0;
    bool selectionMode = false;    // True when caret is shown for item selection
    int editValue = 0;             // Temporary value during editing
    int editField = 0;             // Which field is being edited (for multi-field screens)

    // Time/date edit state
    int editHour = 0;
    int editMinute = 0;
    int editMonth = 1;
    int editDay = 1;
    int editYear = 2025;

    // Schedule edit state
    int editScheduleIndex = -1;     // Which schedule we're editing
    uint8_t editDays = 0x7F;        // Day bitmask
    uint8_t editDuration = 0;       // 0 = use default
    bool editEnabled = true;
    int editSubField = 0;           // Sub-field for time editing (0=hour, 1=minute)
    bool editingField = false;      // True when actively editing a field's value

    // WiFi schedule edit state
    int editWifiScheduleIndex = -1;
    uint8_t editStartHour = 6;
    uint8_t editStartMinute = 0;
    uint8_t editEndHour = 8;
    uint8_t editEndMinute = 0;

    // Rendering methods
    void drawHeader(const char* title);
    void drawOverviewScreen();
    void drawSchedulesScreen();
    void drawScheduleEditScreen();
    void drawSettingsScreen();
    void drawSettingsEditScreen();
    void drawTimeEditScreen();
    void drawDateEditScreen();
    void drawConnectivityScreen();
    void drawWifiSchedulesScreen();
    void drawWifiScheduleEditScreen();
    void drawAboutScreen();
    void drawConfirmResetScreen();
    void drawThrowConfirmScreen();

    // Overview screen elements
    void drawBatteryIndicator(int x, int y);
    void drawConnectionStatus(int x, int y);

    // Navigation helpers
    void drawNavCarets();
    void drawMenuItem(int y, const char* label, const char* value, bool selected);

    // Helper methods
    void clearContent();
    uint16_t getBatteryColor() const;
};

extern Display display;
