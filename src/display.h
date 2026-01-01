#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include "config.h"
#include "buttons.h"

// Display type for 2.9" Waveshare (296x128)
// Using GxEPD2_290_T94_V2 which is compatible with most 2.9" black/white displays
typedef GxEPD2_BW<GxEPD2_290_T94_V2, GxEPD2_290_T94_V2::HEIGHT> EPD_Class;

// UI Screen states (simplified - read-only except WiFi toggle)
enum class Screen {
    OVERVIEW,       // Time, battery, next feed, warnings
    SCHEDULES,      // Read-only feed schedule list
    CONNECTIVITY,   // WiFi/BLE status, QR code, toggle WiFi
    SETTINGS,       // Read-only settings display
    ABOUT           // Version, device ID
};

// Status data passed to display
struct StatusData {
    // Time
    char currentTime[8];        // "HH:MM:SS" or "HH:MM"
    char currentDate[16];       // "Mon Jan 01"

    // Battery
    float batteryVoltage;
    bool isCharging;
    const char* batteryStatus;  // "Good", "Okay", "Low", "Critical"

    // Next feed
    char nextFeedTime[20];      // "Today 07:00" or "Mon 07:00" or "None"

    // Status flags
    bool wifiEnabled;
    bool wifiClientConnected;
    bool bleEnabled;
    bool bleClientConnected;
    bool vacationMode;
    bool timeSynced;

    // WiFi info (for connectivity screen)
    char wifiSSID[32];
    char wifiPassword[16];
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

    // Update status data
    void setStatus(const StatusData& newStatus);

    // Request WiFi toggle (called from button handler, returns true if toggled)
    bool shouldToggleWifi() const { return wifiToggleRequested; }
    void clearWifiToggleRequest() { wifiToggleRequested = false; }

private:
    EPD_Class epd = EPD_Class(GxEPD2_290_T94_V2(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

    Screen currentScreen = Screen::OVERVIEW;
    bool needsRedraw = true;
    bool wifiToggleRequested = false;

    // Partial refresh counter - do full refresh periodically
    uint8_t partialRefreshCount = 0;

    // Scroll state for list screens
    int scrollOffset = 0;
    bool scrollMode = false;

    // Cached status data
    StatusData status = {};

    // Drawing methods for each screen
    void drawOverviewScreen();
    void drawSchedulesScreen();
    void drawConnectivityScreen();
    void drawSettingsScreen();
    void drawAboutScreen();

    // Helper drawing methods
    void drawHeader(const char* title);
    void drawBatteryIcon(int16_t x, int16_t y);
    void drawWifiIcon(int16_t x, int16_t y, bool enabled, bool connected);
    void drawBleIcon(int16_t x, int16_t y, bool enabled, bool connected);
    void drawWarningBanner(const char* message);
    void drawScrollIndicator(int currentItem, int totalItems);
    void drawQRCode(int16_t x, int16_t y, const char* data, int size);

    // Refresh helpers
    void doFullRefresh();
    void doPartialRefresh();
};

extern Display display;
