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

// Display type for 2.9" Waveshare V2 (296x128, Rev2.1) - SSD1680 controller
typedef GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> EPD_Class;

// UI Screen states (read-only, configuration via BLE app)
enum class Screen {
    OVERVIEW,       // Time, battery, next feed, warnings
    FEED_SCHEDULES, // Read-only feed schedule list
    BLE_SCHEDULES,  // Read-only BLE schedule list
    ABOUT           // Version, device info, settings summary
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

    // Force redraw on next update (partial refresh)
    void invalidate() { needsRedraw = true; }

    // Force full refresh on next update (use for content changes to avoid ghosting)
    void forceFullRefresh() { needsRedraw = true; partialRefreshCount = PARTIAL_REFRESH_LIMIT; statusUpdateNeeded = true; }

    // Conditional refresh - only refresh if current screen shows relevant data
    void refreshIfFeedSchedulesAffected() {
        if (currentScreen == Screen::OVERVIEW || currentScreen == Screen::FEED_SCHEDULES) {
            forceFullRefresh();
        }
    }
    void refreshIfBleSchedulesAffected() {
        if (currentScreen == Screen::OVERVIEW || currentScreen == Screen::BLE_SCHEDULES) {
            forceFullRefresh();
        }
    }
    void refreshIfSettingsAffected() {
        if (currentScreen == Screen::OVERVIEW) {
            forceFullRefresh();
        }
    }
    void refreshIfTimeAffected() {
        if (currentScreen == Screen::OVERVIEW) {
            forceFullRefresh();
        }
    }

    // Check if status update is needed before refresh
    bool needsStatusUpdate() const { return statusUpdateNeeded; }
    void clearStatusUpdateFlag() { statusUpdateNeeded = false; }

    // Update status data
    void setStatus(const StatusData& newStatus);


    // Feed countdown warning display
    void showFeedCountdown(int secondsRemaining);  // Show countdown warning
    void showFeedingNow();                          // Show "feeding now" message
    void showFeedCancelled();                       // Show cancelled message

    // BLE pairing PIN display
    void showPairingPin(const char* pin);           // Show pairing PIN prominently
    void hidePairingPin();                          // Clear PIN and return to normal display

    // Request manual feed countdown (called from button handler on Overview screen)
    bool shouldStartFeedCountdown() const { return feedCountdownRequested; }
    void clearFeedCountdownRequest() { feedCountdownRequested = false; }

    // Screensaver - reduces e-paper refresh cycles when idle
    void checkScreensaver();
    bool isScreensaverActive() const { return screensaverActive; }

private:
    EPD_Class epd = EPD_Class(GxEPD2_290_BS(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

    Screen currentScreen = Screen::OVERVIEW;
    bool needsRedraw = true;
    bool feedCountdownRequested = false;
    bool statusUpdateNeeded = false;

    // Screensaver state
    bool screensaverActive = false;
    uint32_t lastButtonActivityTime = 0;

    // Partial refresh counter - do full refresh periodically
    uint8_t partialRefreshCount = 0;

    // Scroll state for list screens
    int scrollOffset = 0;
    bool scrollMode = false;

    // Cached status data
    StatusData status = {};

    // Drawing methods for each screen
    void drawOverviewScreen();
    void drawFeedSchedulesScreen();
    void drawBleSchedulesScreen();
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
    void drawScreensaver();
};

extern Display display;
