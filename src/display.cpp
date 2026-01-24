#include "display.h"
#include "storage.h"
#include "rtc_manager.h"
#include "buttons.h"
#include "icons.h"
#include "sun_calc.h"
#include "time_format.h"
#include <qrcode.h>

Display display;

// Number of screens for circular navigation
constexpr int NUM_SCREENS = 4;

void Display::begin() {
    // Initialize SPI with custom pins (required for ESP32-C6 with non-default pins)
    SPI.begin(PIN_EPD_CLK, -1, PIN_EPD_MOSI, PIN_EPD_CS);

    // Initialize display
    epd.init(115200, true, 50, false);  // serial debug, initial reset, reset duration, pulldown RST

    // Set rotation (0 = portrait, 3 = landscape with connector on right)
    epd.setRotation(3);

    // Set text defaults
    epd.setTextColor(GxEPD_BLACK);
    epd.setFont(&FreeSans9pt7b);

    // Initial full refresh with splash - deer icon + text
    epd.setFullWindow();
    epd.firstPage();
    do {
        epd.fillScreen(GxEPD_WHITE);

        // Draw deer icon on left side, vertically centered
        int iconX = 10;
        int iconY = (SCREEN_HEIGHT - DEER_ICON_HEIGHT) / 2;
        epd.drawBitmap(iconX, iconY, DEER_ICON, DEER_ICON_WIDTH, DEER_ICON_HEIGHT, GxEPD_BLACK);

        // "FeedMe" text to the right of icon
        int16_t x1, y1;
        uint16_t w, h;
        int textX = iconX + DEER_ICON_WIDTH + 15;

        epd.setFont(&FreeSansBold18pt7b);
        epd.setCursor(textX, 55);
        epd.print("FeedMe");

        // Version below
        epd.setFont(&FreeSans9pt7b);
        char verStr[16];
        snprintf(verStr, sizeof(verStr), "v%s", FEEDME_VERSION);
        epd.setCursor(textX, 80);
        epd.print(verStr);
    } while (epd.nextPage());

    delay(1000);

    DEBUG_PRINTLN("Display: E-paper initialized");
    needsRedraw = true;
    // Force full refresh for first screen draw (partial refresh doesn't work on this display)
    partialRefreshCount = PARTIAL_REFRESH_LIMIT;
    // Initialize screensaver timer so it doesn't trigger immediately on boot
    lastButtonActivityTime = millis();
}

void Display::update() {
    if (!needsRedraw) return;
    needsRedraw = false;

    // Decide whether to use partial or full refresh
    bool usePartial = (partialRefreshCount < PARTIAL_REFRESH_LIMIT);

    if (usePartial) {
        doPartialRefresh();
        partialRefreshCount++;
    } else {
        doFullRefresh();
        partialRefreshCount = 0;
    }
}

void Display::doFullRefresh() {
    epd.setFullWindow();
    epd.firstPage();
    do {
        // Poll buttons during refresh so presses are queued
        buttons.update();

        epd.fillScreen(GxEPD_WHITE);

        switch (currentScreen) {
            case Screen::OVERVIEW:
                drawOverviewScreen();
                break;
            case Screen::FEED_SCHEDULES:
                drawFeedSchedulesScreen();
                break;
            case Screen::BLE_SCHEDULES:
                drawBleSchedulesScreen();
                break;
            case Screen::ABOUT:
                drawAboutScreen();
                break;
        }
    } while (epd.nextPage());
}

void Display::doPartialRefresh() {
    // For Overview screen, try targeted partial refresh on time area
    if (currentScreen == Screen::OVERVIEW) {
        // Calculate time area position (depends on warning banners)
        int yOffset = 24;  // Header height
        if (!status.timeSynced) yOffset += 16;
        if (status.vacationMode) yOffset += 16;

        // Time display area: x=10, y=yOffset, width=140, height=32
        // Round to 8-pixel boundary as required by many e-paper controllers
        int timeY = yOffset;
        int timeHeight = 32;

        // Use targeted partial window for just the time area
        epd.setPartialWindow(0, timeY, 165, timeHeight);
        epd.firstPage();
        do {
            // Poll buttons during refresh so presses are queued
            buttons.update();

            // Clear just this region
            epd.fillRect(0, timeY, 160, timeHeight, GxEPD_WHITE);

            // Draw time (12-hour format)
            epd.setFont(&FreeSansBold18pt7b);
            epd.setTextColor(GxEPD_BLACK);
            epd.setCursor(5, timeY + 28);

            int hour = (status.currentTime[0] - '0') * 10 + (status.currentTime[1] - '0');
            bool isPM = hour >= 12;
            int hour12 = hour % 12;
            if (hour12 == 0) hour12 = 12;

            char time12[12];
            snprintf(time12, sizeof(time12), "%d:%c%c %s",
                     hour12, status.currentTime[3], status.currentTime[4],
                     isPM ? "PM" : "AM");
            epd.print(time12);
        } while (epd.nextPage());
    } else {
        // For other screens, do full partial refresh
        epd.setPartialWindow(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        epd.firstPage();
        do {
            // Poll buttons during refresh so presses are queued
            buttons.update();

            epd.fillScreen(GxEPD_WHITE);

            switch (currentScreen) {
                case Screen::FEED_SCHEDULES:
                    drawFeedSchedulesScreen();
                    break;
                case Screen::BLE_SCHEDULES:
                    drawBleSchedulesScreen();
                    break;
                case Screen::ABOUT:
                    drawAboutScreen();
                    break;
                default:
                    break;
            }
        } while (epd.nextPage());
    }
}

void Display::setScreen(Screen screen) {
    if (currentScreen != screen) {
        currentScreen = screen;
        scrollOffset = 0;
        scrollMode = false;
        needsRedraw = true;
        // Force full refresh on screen change to clear previous content
        partialRefreshCount = PARTIAL_REFRESH_LIMIT;
    }
}

void Display::setStatus(const StatusData& newStatus) {
    // Skip time-based redraws while in screensaver (reduces unnecessary refreshes)
    if (screensaverActive) {
        status = newStatus;  // Still update cached status
        return;
    }

    // Trigger redraw if time changed - but only on Overview screen which shows time
    if (strncmp(status.currentTime, newStatus.currentTime, 5) != 0) {
        if (currentScreen == Screen::OVERVIEW) {
            needsRedraw = true;
            partialRefreshCount = PARTIAL_REFRESH_LIMIT;
        }
    }

    status = newStatus;
}

void Display::handleButton(ButtonEvent event) {
    // Any button activity resets screensaver timer
    lastButtonActivityTime = millis();

    // Wake from screensaver on any button press - go to Overview
    if (screensaverActive) {
        screensaverActive = false;
        currentScreen = Screen::OVERVIEW;  // Always wake to Overview
        needsRedraw = true;
        partialRefreshCount = PARTIAL_REFRESH_LIMIT;  // Full refresh to clear screensaver
        return;  // Don't process as navigation
    }

    if (scrollMode) {
        // In scroll mode, buttons scroll the list
        int maxScroll = 0;  // Will be set based on current screen

        if (currentScreen == Screen::FEED_SCHEDULES) {
            maxScroll = max(0, (int)storage.getScheduleCount() - 3);  // Show 3 items at a time
        } else if (currentScreen == Screen::BLE_SCHEDULES) {
            maxScroll = max(0, (int)storage.getBleScheduleCount() - 3);
        }

        switch (event) {
            case ButtonEvent::PREV_PRESS:
                if (scrollOffset > 0) {
                    scrollOffset--;
                    needsRedraw = true;
                }
                break;

            case ButtonEvent::NEXT_PRESS:
                if (scrollOffset < maxScroll) {
                    scrollOffset++;
                    needsRedraw = true;
                } else {
                    // Wrap to top in single-button mode for continuous scrolling
                    scrollOffset = 0;
                    needsRedraw = true;
                }
                break;

            case ButtonEvent::PREV_HOLD:
            case ButtonEvent::NEXT_HOLD:
                // Exit scroll mode (either hold works - supports single-button mode)
                scrollMode = false;
                needsRedraw = true;
                break;

            default:
                break;
        }
    } else {
        // Normal navigation mode
        switch (event) {
            case ButtonEvent::PREV_PRESS:
                // Previous screen (circular)
                {
                    int idx = static_cast<int>(currentScreen);
                    idx = (idx - 1 + NUM_SCREENS) % NUM_SCREENS;
                    setScreen(static_cast<Screen>(idx));
                }
                break;

            case ButtonEvent::NEXT_PRESS:
                // Next screen (circular)
                {
                    int idx = static_cast<int>(currentScreen);
                    idx = (idx + 1) % NUM_SCREENS;
                    setScreen(static_cast<Screen>(idx));
                }
                break;

            case ButtonEvent::NEXT_HOLD:
                // Context action based on screen
                if (currentScreen == Screen::OVERVIEW) {
                    // Request manual feed countdown - main loop will handle the countdown
                    feedCountdownRequested = true;
                    // Don't redraw - main.cpp will handle countdown display
                } else if (currentScreen == Screen::FEED_SCHEDULES) {
                    // Enter scroll mode for feed schedules
                    if (storage.getScheduleCount() > 3) {
                        scrollMode = true;
                        needsRedraw = true;
                    }
                } else if (currentScreen == Screen::BLE_SCHEDULES) {
                    // Enter scroll mode for BLE schedules
                    if (storage.getBleScheduleCount() > 3) {
                        scrollMode = true;
                        needsRedraw = true;
                    }
                }
                break;

            default:
                break;
        }
    }
}

// =============================================================================
// Screen Drawing Methods
// =============================================================================

void Display::drawOverviewScreen() {
    // Header
    drawHeader("FeedMe");

    // Status icons in header area (right side)
    drawBatteryIcon(SCREEN_WIDTH - 45, 2);
    drawBleIcon(SCREEN_WIDTH - 70, 2, status.bleEnabled, status.bleClientConnected);

    // Warning banner if time not synced
    if (!status.timeSynced) {
        drawWarningBanner("Time not synced");
    }

    // Vacation mode banner
    if (status.vacationMode) {
        int bannerY = status.timeSynced ? 24 : 40;  // Below header (24px) or warning (24+16=40)
        epd.fillRect(0, bannerY, SCREEN_WIDTH, 16, GxEPD_BLACK);
        epd.setTextColor(GxEPD_WHITE);
        epd.setFont(&FreeMono9pt7b);
        int16_t x1, y1;
        uint16_t w, h;
        epd.getTextBounds("VACATION MODE", 0, 0, &x1, &y1, &w, &h);
        epd.setCursor((SCREEN_WIDTH - w) / 2, bannerY + 12);
        epd.print("VACATION MODE");
        epd.setTextColor(GxEPD_BLACK);
    }

    int yOffset = 24;  // Start below header
    if (!status.timeSynced) yOffset += 16;  // Warning banner height
    if (status.vacationMode) yOffset += 16;  // Vacation banner height

    // Large time display (convert to 12-hour format)
    epd.setFont(&FreeSansBold18pt7b);
    epd.setCursor(5, yOffset + 34);

    // Parse hour from status.currentTime (format "HH:MM" or "HH:MM:SS")
    int hour = (status.currentTime[0] - '0') * 10 + (status.currentTime[1] - '0');
    bool isPM = hour >= 12;
    int hour12 = hour % 12;
    if (hour12 == 0) hour12 = 12;

    char time12[12];
    snprintf(time12, sizeof(time12), "%d:%c%c %s",
             hour12, status.currentTime[3], status.currentTime[4],
             isPM ? "PM" : "AM");
    epd.print(time12);

    // Date
    epd.setFont(&FreeMono9pt7b);
    epd.setCursor(170, yOffset + 26);
    epd.print(status.currentDate);

    // Battery info
    epd.setCursor(5, yOffset + 56);
    epd.print("Batt: ");
    epd.print(status.batteryVoltage, 1);
    epd.print("V ");
    epd.print(status.batteryStatus);
    if (status.isCharging) {
        epd.print(" CHG");
    }

    // Next feed time
    epd.setCursor(5, yOffset + 74);
    epd.print("Next: ");
    epd.print(status.nextFeedTime);

    // Action hint at bottom (only if no banners pushing content down)
    if (status.timeSynced && !status.vacationMode) {
        epd.setCursor(5, 122);
        epd.print("Hold to test feed now");
    }
}

void Display::drawFeedSchedulesScreen() {
    drawHeader("Feed Schedules");

    int scheduleCount = storage.getScheduleCount();

    if (scheduleCount == 0) {
        epd.setFont(&FreeMono9pt7b);
        int16_t x1, y1;
        uint16_t w, h;

        epd.getTextBounds("No schedules configured", 0, 0, &x1, &y1, &w, &h);
        epd.setCursor((SCREEN_WIDTH - w) / 2, 65);
        epd.print("No schedules configured");

        epd.getTextBounds("Use FeedMe app to add", 0, 0, &x1, &y1, &w, &h);
        epd.setCursor((SCREEN_WIDTH - w) / 2, 83);
        epd.print("Use FeedMe app to add");
        return;
    }

    // Get current date for sunrise/sunset calculations
    Settings& settings = storage.getSettings();
    int16_t tzOffset = settings.timezoneOffset;
    DateTime now = rtcManager.now();

    // Display schedules (4 visible at a time with compact layout)
    int visibleCount = min(4, scheduleCount - scrollOffset);
    int y = 42;  // Start below 24px header + margin

    // Use monospace font for aligned columns
    epd.setFont(&FreeMono9pt7b);

    for (int i = 0; i < visibleCount; i++) {
        int scheduleIdx = scrollOffset + i;
        Schedule* sched = storage.getSchedule(scheduleIdx);
        if (sched) {
            Schedule& schedule = *sched;

            char line[32];
            char daysStr[8];
            char timeStr[8];

            // Format days string
            if (schedule.days == DAYS_ALL) {
                strcpy(daysStr, "Daily ");
            } else {
                const char* dayLetters = "SMTWTFS";
                for (int d = 0; d < 7; d++) {
                    daysStr[d] = (schedule.days & (1 << d)) ? dayLetters[d] : '-';
                }
                daysStr[7] = '\0';
            }

            // Calculate display time based on schedule type
            if (schedule.scheduleType == ScheduleType::SPECIFIC_TIME) {
                // Convert stored local time for display
                snprintf(timeStr, sizeof(timeStr), "%02d:%02d", schedule.hour, schedule.minute);
            } else if (settings.locationSet) {
                // Calculate sunrise/sunset time
                int sunMinutes;
                if (schedule.scheduleType == ScheduleType::SUNRISE) {
                    sunMinutes = SunCalc::getSunrise(now.year(), now.month(), now.day(),
                                                      settings.latitude, settings.longitude, tzOffset);
                } else {  // SUNSET
                    sunMinutes = SunCalc::getSunset(now.year(), now.month(), now.day(),
                                                     settings.latitude, settings.longitude, tzOffset);
                }

                if (sunMinutes >= 0) {
                    // Apply offset
                    sunMinutes += schedule.sunOffset;
                    while (sunMinutes < 0) sunMinutes += 1440;
                    while (sunMinutes >= 1440) sunMinutes -= 1440;

                    int hour = sunMinutes / 60;
                    int minute = sunMinutes % 60;
                    // Show with sun indicator (*) to indicate calculated time
                    snprintf(timeStr, sizeof(timeStr), "*%02d:%02d", hour, minute);
                } else {
                    // Sun doesn't rise/set at this location
                    strcpy(timeStr, "--:--");
                }
            } else {
                // Location not set, show schedule type indicator
                if (schedule.scheduleType == ScheduleType::SUNRISE) {
                    if (schedule.sunOffset != 0) {
                        snprintf(timeStr, sizeof(timeStr), "SR%+d", schedule.sunOffset);
                    } else {
                        strcpy(timeStr, "SR   ");
                    }
                } else {
                    if (schedule.sunOffset != 0) {
                        snprintf(timeStr, sizeof(timeStr), "SS%+d", schedule.sunOffset);
                    } else {
                        strcpy(timeStr, "SS   ");
                    }
                }
            }

            int dur = (schedule.duration > 0) ? schedule.duration : settings.motorDuration;
            snprintf(line, sizeof(line), "%-6s %s %2ds %s",
                     timeStr, daysStr, dur,
                     schedule.enabled ? "ON" : "--");

            epd.setCursor(5, y);
            epd.print(line);

            y += 18;
        }
    }

    // Scroll hint at bottom
    if (scheduleCount > 4) {
        drawScrollIndicator(scrollOffset, scheduleCount - 4);
        epd.setCursor(5, 122);
        epd.print(scrollMode ? "Scrolling - Hold to exit" : "Hold to scroll");
    }
}

void Display::drawBleSchedulesScreen() {
    drawHeader("BLE Schedules");

    int scheduleCount = storage.getBleScheduleCount();

    if (scheduleCount == 0) {
        epd.setFont(&FreeMono9pt7b);
        int16_t x1, y1;
        uint16_t w, h;

        epd.getTextBounds("! BLE always on !", 0, 0, &x1, &y1, &w, &h);
        epd.setCursor((SCREEN_WIDTH - w) / 2, 55);
        epd.print("! BLE always on !");

        epd.getTextBounds("Higher battery usage", 0, 0, &x1, &y1, &w, &h);
        epd.setCursor((SCREEN_WIDTH - w) / 2, 73);
        epd.print("Higher battery usage");

        epd.getTextBounds("Add schedule in app", 0, 0, &x1, &y1, &w, &h);
        epd.setCursor((SCREEN_WIDTH - w) / 2, 91);
        epd.print("Add schedule in app");
        return;
    }

    // Display schedules (4 visible at a time with compact layout)
    int visibleCount = min(4, scheduleCount - scrollOffset);
    int y = 42;  // Start below 24px header + margin

    // Use monospace font for aligned columns
    epd.setFont(&FreeMono9pt7b);

    for (int i = 0; i < visibleCount; i++) {
        int scheduleIdx = scrollOffset + i;
        BleSchedule* sched = storage.getBleSchedule(scheduleIdx);
        if (sched) {
            BleSchedule& schedule = *sched;

            // Format: "06:00-18:00 Daily  ON" or "06:00-18:00 S-T-T-- --"
            char line[32];
            char daysStr[8];

            if (schedule.days == DAYS_ALL) {
                strcpy(daysStr, "Daily ");
            } else {
                const char* dayLetters = "SMTWTFS";
                for (int d = 0; d < 7; d++) {
                    daysStr[d] = (schedule.days & (1 << d)) ? dayLetters[d] : '-';
                }
                daysStr[7] = '\0';
            }

            snprintf(line, sizeof(line), "%02d:%02d-%02d:%02d %s %s",
                     schedule.startHour, schedule.startMinute,
                     schedule.endHour, schedule.endMinute,
                     daysStr,
                     schedule.enabled ? "ON" : "--");

            epd.setCursor(5, y);
            epd.print(line);

            y += 18;
        }
    }

    // Scroll hint at bottom
    if (scheduleCount > 4) {
        drawScrollIndicator(scrollOffset, scheduleCount - 4);
        epd.setCursor(5, 122);
        epd.print(scrollMode ? "Scrolling - Hold to exit" : "Hold to scroll");
    }
}

void Display::drawAboutScreen() {
    drawHeader("About");

    epd.setFont(&FreeMono9pt7b);
    Settings& settings = storage.getSettings();
    char line[40];
    int y = 44;

    // Version
    snprintf(line, sizeof(line), "Version: %s", FEEDME_VERSION);
    epd.setCursor(5, y);
    epd.print(line);

    // Device ID
    y += 18;
    snprintf(line, sizeof(line), "ID: %s", storage.getDeviceId());
    epd.setCursor(5, y);
    epd.print(line);

    // Device Name
    y += 18;
    epd.setCursor(5, y);
    if (strlen(settings.deviceName) > 0) {
        snprintf(line, sizeof(line), "Name: %.20s", settings.deviceName);
    } else {
        snprintf(line, sizeof(line), "Name: FeedMe-%s", storage.getDeviceId());
    }
    epd.print(line);

    // Battery type and detected voltage (12V vs 6V)
    y += 18;
    const char* battType = (settings.batteryType == BatteryType::AGM) ? "AGM" :
                           (settings.batteryType == BatteryType::GEL) ? "GEL" : "SLA";
    const char* battVolt = (status.batteryVoltage > BATTERY_TYPE_THRESHOLD) ? "12V" : "6V";
    snprintf(line, sizeof(line), "Battery: %s %s", battType, battVolt);
    epd.setCursor(5, y);
    epd.print(line);

    // Antenna
    y += 18;
    const char* antStr = (settings.antennaType == AntennaType::ONBOARD) ? "Onboard" : "External";
    snprintf(line, sizeof(line), "Antenna: %s", antStr);
    epd.setCursor(5, y);
    epd.print(line);
}

// =============================================================================
// Helper Drawing Methods
// =============================================================================

void Display::drawHeader(const char* title) {
    // Header bar (24px tall)
    epd.fillRect(0, 0, SCREEN_WIDTH, 24, GxEPD_BLACK);
    epd.setTextColor(GxEPD_WHITE);
    epd.setFont(&FreeSansBold12pt7b);
    epd.setCursor(5, 19);  // ~3px top margin, ~5px bottom margin
    epd.print(title);
    epd.setTextColor(GxEPD_BLACK);
}

void Display::drawBatteryIcon(int16_t x, int16_t y) {
    // Battery outline (white on black header)
    epd.drawRect(x, y + 3, 20, 10, GxEPD_WHITE);
    epd.fillRect(x + 20, y + 5, 2, 6, GxEPD_WHITE);

    // Fill based on status
    int fillWidth = 0;
    if (strcmp(status.batteryStatus, "Good") == 0) fillWidth = 16;
    else if (strcmp(status.batteryStatus, "Okay") == 0) fillWidth = 10;
    else if (strcmp(status.batteryStatus, "Low") == 0) fillWidth = 5;
    else fillWidth = 2;  // Critical

    if (fillWidth > 0) {
        epd.fillRect(x + 2, y + 5, fillWidth, 6, GxEPD_WHITE);
    }
}

void Display::drawWifiIcon(int16_t x, int16_t y, bool enabled, bool connected) {
    // Shift up to align with battery/BLE icons
    int cy = y + 12;

    if (!enabled) {
        // X mark for disabled (thicker)
        epd.drawLine(x, y + 2, x + 12, y + 14, GxEPD_WHITE);
        epd.drawLine(x + 1, y + 2, x + 13, y + 14, GxEPD_WHITE);
        epd.drawLine(x + 12, y + 2, x, y + 14, GxEPD_WHITE);
        epd.drawLine(x + 13, y + 2, x + 1, y + 14, GxEPD_WHITE);
        return;
    }

    // Solid dot at bottom for WiFi
    epd.fillCircle(x + 7, cy, 2, GxEPD_WHITE);

    // Draw thick arcs using multiple concentric circles
    // Inner arc
    for (int r = 5; r <= 6; r++) {
        for (int angle = 220; angle <= 320; angle += 5) {
            float rad = angle * PI / 180.0;
            int px = x + 7 + (int)(r * cos(rad));
            int py = cy + (int)(r * sin(rad));
            epd.drawPixel(px, py, GxEPD_WHITE);
        }
    }
    // Outer arc
    for (int r = 9; r <= 10; r++) {
        for (int angle = 220; angle <= 320; angle += 5) {
            float rad = angle * PI / 180.0;
            int px = x + 7 + (int)(r * cos(rad));
            int py = cy + (int)(r * sin(rad));
            epd.drawPixel(px, py, GxEPD_WHITE);
        }
    }
}

void Display::drawBleIcon(int16_t x, int16_t y, bool enabled, bool connected) {
    if (!enabled) return;

    // Simple BLE symbol (Bluetooth rune shape)
    // Vertical line
    epd.drawLine(x + 5, y + 2, x + 5, y + 14, GxEPD_WHITE);
    // Upper triangle
    epd.drawLine(x + 5, y + 2, x + 10, y + 6, GxEPD_WHITE);
    epd.drawLine(x + 10, y + 6, x + 5, y + 8, GxEPD_WHITE);
    // Lower triangle
    epd.drawLine(x + 5, y + 8, x + 10, y + 10, GxEPD_WHITE);
    epd.drawLine(x + 10, y + 10, x + 5, y + 14, GxEPD_WHITE);
    // Connection arrows
    epd.drawLine(x, y + 6, x + 5, y + 8, GxEPD_WHITE);
    epd.drawLine(x, y + 10, x + 5, y + 8, GxEPD_WHITE);
}

void Display::drawWarningBanner(const char* message) {
    // Warning banner below header (header is 24px)
    epd.fillRect(0, 24, SCREEN_WIDTH, 16, GxEPD_BLACK);
    epd.setTextColor(GxEPD_WHITE);
    epd.setFont(&FreeMono9pt7b);

    // Center the text
    int16_t x1, y1;
    uint16_t w, h;
    epd.getTextBounds(message, 0, 0, &x1, &y1, &w, &h);
    epd.setCursor((SCREEN_WIDTH - w) / 2, 37);
    epd.print(message);

    epd.setTextColor(GxEPD_BLACK);
}

void Display::drawScrollIndicator(int currentItem, int totalItems) {
    if (totalItems <= 0) return;

    // Vertical scroll bar on right edge
    int barHeight = 60;
    int barY = 30;
    int thumbHeight = max(10, barHeight / (totalItems + 1));
    int thumbY = barY + (currentItem * (barHeight - thumbHeight)) / max(1, totalItems);

    epd.drawRect(SCREEN_WIDTH - 8, barY, 6, barHeight, GxEPD_BLACK);
    epd.fillRect(SCREEN_WIDTH - 7, thumbY, 4, thumbHeight, GxEPD_BLACK);
}

void Display::drawQRCode(int16_t x, int16_t y, const char* data, int pixelSize) {
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(3)];

    if (qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, data) != 0) {
        // Failed to generate QR code
        return;
    }

    int size = qrcode.size;

    // Draw QR code
    for (int qy = 0; qy < size; qy++) {
        for (int qx = 0; qx < size; qx++) {
            if (qrcode_getModule(&qrcode, qx, qy)) {
                epd.fillRect(x + qx * pixelSize, y + qy * pixelSize,
                            pixelSize, pixelSize, GxEPD_BLACK);
            }
        }
    }
}

// =============================================================================
// Feed Countdown Warning Display
// =============================================================================

void Display::showFeedCountdown(int secondsRemaining) {
    epd.setFullWindow();
    epd.firstPage();
    do {
        epd.fillScreen(GxEPD_WHITE);

        int16_t x1, y1;
        uint16_t w, h;

        // Warning header (centered)
        epd.fillRect(0, 0, SCREEN_WIDTH, 24, GxEPD_BLACK);
        epd.setTextColor(GxEPD_WHITE);
        epd.setFont(&FreeSansBold12pt7b);
        const char* header = "!! WARNING !!";
        epd.getTextBounds(header, 0, 0, &x1, &y1, &w, &h);
        epd.setCursor((SCREEN_WIDTH - w) / 2, 19);
        epd.print(header);
        epd.setTextColor(GxEPD_BLACK);

        // Stand back message (centered)
        epd.setFont(&FreeSansBold12pt7b);
        const char* standBack = "STAND BACK!";
        epd.getTextBounds(standBack, 0, 0, &x1, &y1, &w, &h);
        epd.setCursor((SCREEN_WIDTH - w) / 2, 55);
        epd.print(standBack);

        // Countdown (centered)
        epd.setFont(&FreeSansBold18pt7b);
        char countText[20];
        snprintf(countText, sizeof(countText), "Feed in %ds", secondsRemaining);
        epd.getTextBounds(countText, 0, 0, &x1, &y1, &w, &h);
        epd.setCursor((SCREEN_WIDTH - w) / 2, 95);
        epd.print(countText);

        // Cancel hint (centered)
        epd.setFont(&FreeMono9pt7b);
        const char* hint = "Press button to cancel";
        epd.getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
        epd.setCursor((SCREEN_WIDTH - w) / 2, 122);
        epd.print(hint);

    } while (epd.nextPage());
}

void Display::showFeedingNow() {
    epd.setFullWindow();
    epd.firstPage();
    do {
        epd.fillScreen(GxEPD_WHITE);

        int16_t x1, y1;
        uint16_t w, h;

        // Warning header (centered)
        epd.fillRect(0, 0, SCREEN_WIDTH, 24, GxEPD_BLACK);
        epd.setTextColor(GxEPD_WHITE);
        epd.setFont(&FreeSansBold12pt7b);
        const char* header = "!! WARNING !!";
        epd.getTextBounds(header, 0, 0, &x1, &y1, &w, &h);
        epd.setCursor((SCREEN_WIDTH - w) / 2, 19);
        epd.print(header);
        epd.setTextColor(GxEPD_BLACK);

        // Feeding message (centered)
        epd.setFont(&FreeSansBold18pt7b);
        const char* msg = "FEEDING NOW!";
        epd.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
        epd.setCursor((SCREEN_WIDTH - w) / 2, 80);
        epd.print(msg);

    } while (epd.nextPage());
}

void Display::showFeedCancelled() {
    epd.setFullWindow();
    epd.firstPage();
    do {
        epd.fillScreen(GxEPD_WHITE);

        int16_t x1, y1;
        uint16_t w, h;

        // Header (centered)
        epd.fillRect(0, 0, SCREEN_WIDTH, 24, GxEPD_BLACK);
        epd.setTextColor(GxEPD_WHITE);
        epd.setFont(&FreeSansBold12pt7b);
        const char* header = "Feed Test";
        epd.getTextBounds(header, 0, 0, &x1, &y1, &w, &h);
        epd.setCursor((SCREEN_WIDTH - w) / 2, 19);
        epd.print(header);
        epd.setTextColor(GxEPD_BLACK);

        // Cancelled message (centered)
        epd.setFont(&FreeSansBold18pt7b);
        const char* msg = "Cancelled";
        epd.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
        epd.setCursor((SCREEN_WIDTH - w) / 2, 80);
        epd.print(msg);

    } while (epd.nextPage());
}

// =============================================================================
// BLE Pairing PIN Display
// =============================================================================

void Display::showPairingPin(const char* pin) {
    epd.setFullWindow();
    epd.firstPage();
    do {
        epd.fillScreen(GxEPD_WHITE);

        int16_t x1, y1;
        uint16_t w, h;

        // Header (black bar with white text)
        epd.fillRect(0, 0, SCREEN_WIDTH, 28, GxEPD_BLACK);
        epd.setTextColor(GxEPD_WHITE);
        epd.setFont(&FreeSansBold12pt7b);
        const char* header = "Pairing Request";
        epd.getTextBounds(header, 0, 0, &x1, &y1, &w, &h);
        epd.setCursor((SCREEN_WIDTH - w) / 2, 22);
        epd.print(header);
        epd.setTextColor(GxEPD_BLACK);

        // Instruction text
        epd.setFont(&FreeSans9pt7b);
        const char* instruction = "Enter this PIN in the app:";
        epd.getTextBounds(instruction, 0, 0, &x1, &y1, &w, &h);
        epd.setCursor((SCREEN_WIDTH - w) / 2, 52);
        epd.print(instruction);

        // Large PIN display (centered, with spacing between digits)
        epd.setFont(&FreeSansBold18pt7b);

        // Calculate total width with spacing
        char digitStr[2] = {0, 0};
        uint16_t totalWidth = 0;
        uint16_t digitWidths[4];
        uint16_t spacing = 16;  // Space between digits

        for (int i = 0; i < 4 && pin[i]; i++) {
            digitStr[0] = pin[i];
            epd.getTextBounds(digitStr, 0, 0, &x1, &y1, &digitWidths[i], &h);
            totalWidth += digitWidths[i];
        }
        totalWidth += spacing * 3;  // 3 gaps between 4 digits

        // Draw each digit
        int16_t xPos = (SCREEN_WIDTH - totalWidth) / 2;
        int16_t yPos = 95;

        for (int i = 0; i < 4 && pin[i]; i++) {
            digitStr[0] = pin[i];
            epd.setCursor(xPos, yPos);
            epd.print(digitStr);
            xPos += digitWidths[i] + spacing;
        }

        // Bottom instruction
        epd.setFont(&FreeSans9pt7b);
        const char* bottom = "PIN expires on disconnect";
        epd.getTextBounds(bottom, 0, 0, &x1, &y1, &w, &h);
        epd.setCursor((SCREEN_WIDTH - w) / 2, 120);
        epd.print(bottom);

    } while (epd.nextPage());

    DEBUG_PRINTF("Display: Showing pairing PIN: %s\n", pin);
}

void Display::hidePairingPin() {
    // Return to normal display by forcing a full refresh of the current screen
    forceFullRefresh();
    DEBUG_PRINTLN("Display: Hiding pairing PIN, returning to normal display");
}

// =============================================================================
// Screensaver
// =============================================================================

constexpr uint32_t SCREENSAVER_TIMEOUT_MS = 60 * 1000;  // 60 seconds

void Display::checkScreensaver() {
    if (screensaverActive) {
        return;  // Already in screensaver mode
    }

    uint32_t now = millis();
    if (now - lastButtonActivityTime > SCREENSAVER_TIMEOUT_MS) {
        screensaverActive = true;
        drawScreensaver();
    }
}

void Display::drawScreensaver() {
    epd.setFullWindow();
    epd.firstPage();
    do {
        buttons.update();  // Keep polling buttons during refresh

        epd.fillScreen(GxEPD_WHITE);

        // Draw deer icon on left side, vertically centered
        int iconX = 10;
        int iconY = (SCREEN_HEIGHT - DEER_ICON_HEIGHT) / 2;
        epd.drawBitmap(iconX, iconY, DEER_ICON, DEER_ICON_WIDTH, DEER_ICON_HEIGHT, GxEPD_BLACK);

        // "FeedMe" text to the right of icon
        int textX = iconX + DEER_ICON_WIDTH + 15;

        epd.setFont(&FreeSansBold18pt7b);
        epd.setCursor(textX, 50);
        epd.print("FeedMe");

        // "Press button to wake" below
        epd.setFont(&FreeSans9pt7b);
        epd.setCursor(textX, 75);
        epd.print("Press button");
        epd.setCursor(textX, 93);
        epd.print("to wake");
    } while (epd.nextPage());
}
