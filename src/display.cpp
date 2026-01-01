#include "display.h"
#include "storage.h"
#include "rtc_manager.h"
#include <qrcode.h>

Display display;

// Number of screens for circular navigation
constexpr int NUM_SCREENS = 5;

void Display::begin() {
    // Initialize SPI for e-paper
    SPI.begin(PIN_EPD_CLK, -1, PIN_EPD_MOSI, PIN_EPD_CS);

    // Initialize display
    epd.init(115200, true, 50, false);  // serial debug, initial reset, reset duration, pulldown RST

    // Set rotation (0 or 2 for landscape on 2.9")
    epd.setRotation(0);

    // Set text defaults
    epd.setTextColor(GxEPD_BLACK);
    epd.setFont(&FreeSans9pt7b);

    // Initial full refresh with splash
    epd.setFullWindow();
    epd.firstPage();
    do {
        epd.fillScreen(GxEPD_WHITE);
        epd.setFont(&FreeSansBold18pt7b);
        epd.setCursor(80, 70);
        epd.print("FeedMe");
        epd.setFont(&FreeSans9pt7b);
        epd.setCursor(100, 100);
        epd.print("v");
        epd.print(FEEDME_VERSION);
    } while (epd.nextPage());

    delay(1000);

    Serial.println("Display: E-paper initialized");
    needsRedraw = true;
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
        epd.fillScreen(GxEPD_WHITE);

        switch (currentScreen) {
            case Screen::OVERVIEW:
                drawOverviewScreen();
                break;
            case Screen::SCHEDULES:
                drawSchedulesScreen();
                break;
            case Screen::CONNECTIVITY:
                drawConnectivityScreen();
                break;
            case Screen::SETTINGS:
                drawSettingsScreen();
                break;
            case Screen::ABOUT:
                drawAboutScreen();
                break;
        }
    } while (epd.nextPage());
}

void Display::doPartialRefresh() {
    epd.setPartialWindow(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    epd.firstPage();
    do {
        epd.fillScreen(GxEPD_WHITE);

        switch (currentScreen) {
            case Screen::OVERVIEW:
                drawOverviewScreen();
                break;
            case Screen::SCHEDULES:
                drawSchedulesScreen();
                break;
            case Screen::CONNECTIVITY:
                drawConnectivityScreen();
                break;
            case Screen::SETTINGS:
                drawSettingsScreen();
                break;
            case Screen::ABOUT:
                drawAboutScreen();
                break;
        }
    } while (epd.nextPage());
}

void Display::setScreen(Screen screen) {
    if (currentScreen != screen) {
        currentScreen = screen;
        scrollOffset = 0;
        scrollMode = false;
        needsRedraw = true;
    }
}

void Display::setStatus(const StatusData& newStatus) {
    status = newStatus;
}

void Display::handleButton(ButtonEvent event) {
    if (scrollMode) {
        // In scroll mode, buttons scroll the list
        int maxScroll = 0;  // Will be set based on current screen

        if (currentScreen == Screen::SCHEDULES) {
            maxScroll = max(0, (int)storage.getScheduleCount() - 3);  // Show 3 items at a time
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
                if (currentScreen == Screen::SCHEDULES) {
                    // Enter scroll mode
                    if (storage.getScheduleCount() > 3) {
                        scrollMode = true;
                        needsRedraw = true;
                    }
                } else if (currentScreen == Screen::CONNECTIVITY) {
                    // Toggle WiFi
                    wifiToggleRequested = true;
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
    drawWifiIcon(SCREEN_WIDTH - 70, 2, status.wifiEnabled, status.wifiClientConnected);
    drawBleIcon(SCREEN_WIDTH - 90, 2, status.bleEnabled, status.bleClientConnected);

    // Warning banner if time not synced
    if (!status.timeSynced) {
        drawWarningBanner("! Time not synced - Connect WiFi");
    }

    // Vacation mode banner
    if (status.vacationMode) {
        int bannerY = status.timeSynced ? 22 : 42;
        epd.fillRect(0, bannerY, SCREEN_WIDTH, 18, GxEPD_BLACK);
        epd.setTextColor(GxEPD_WHITE);
        epd.setFont(&FreeSans9pt7b);
        epd.setCursor(70, bannerY + 14);
        epd.print("VACATION MODE");
        epd.setTextColor(GxEPD_BLACK);
    }

    int yOffset = 22;
    if (!status.timeSynced) yOffset += 20;
    if (status.vacationMode) yOffset += 20;

    // Large time display
    epd.setFont(&FreeSansBold18pt7b);
    epd.setCursor(10, yOffset + 28);
    epd.print(status.currentTime);

    // Date
    epd.setFont(&FreeSans9pt7b);
    epd.setCursor(150, yOffset + 23);
    epd.print(status.currentDate);

    // Battery info
    epd.setCursor(10, yOffset + 53);
    epd.print("Battery: ");
    epd.print(status.batteryVoltage, 1);
    epd.print("V (");
    epd.print(status.batteryStatus);
    epd.print(")");
    if (status.isCharging) {
        epd.print(" CHG");
    }

    // Next feed time
    epd.setCursor(10, yOffset + 73);
    epd.print("Next Feed: ");
    epd.print(status.nextFeedTime);
}

void Display::drawSchedulesScreen() {
    drawHeader("Feed Schedules");

    int scheduleCount = storage.getScheduleCount();

    if (scheduleCount == 0) {
        epd.setFont(&FreeSans9pt7b);
        epd.setCursor(50, 70);
        epd.print("No schedules configured");
        epd.setCursor(55, 90);
        epd.print("Use WiFi to add schedules");
        return;
    }

    // Display schedules (3 visible at a time)
    int visibleCount = min(3, scheduleCount - scrollOffset);
    int y = 30;

    for (int i = 0; i < visibleCount; i++) {
        int scheduleIdx = scrollOffset + i;
        Schedule* sched = storage.getSchedule(scheduleIdx);
        if (sched) {
            Schedule& schedule = *sched;
            // Schedule row
            epd.setFont(&FreeSans9pt7b);

            // Time
            epd.setCursor(5, y + 15);
            char timeStr[8];
            snprintf(timeStr, sizeof(timeStr), "%02d:%02d", schedule.hour, schedule.minute);
            epd.print(timeStr);

            // Days (abbreviated)
            epd.setCursor(65, y + 15);
            if (schedule.days == 0x7F) {
                epd.print("Daily");
            } else {
                const char* dayLetters = "SMTWTFS";
                for (int d = 0; d < 7; d++) {
                    if (schedule.days & (1 << d)) {
                        epd.print(dayLetters[d]);
                    } else {
                        epd.print("-");
                    }
                }
            }

            // Duration
            epd.setCursor(140, y + 15);
            if (schedule.duration > 0) {
                epd.print(schedule.duration);
                epd.print("s");
            } else {
                epd.print("Def");
            }

            // Enabled indicator
            epd.setCursor(185, y + 15);
            epd.print(schedule.enabled ? "[ON]" : "[off]");

            // Name (truncated)
            if (strlen(schedule.name) > 0) {
                epd.setCursor(230, y + 15);
                char truncName[8];
                strncpy(truncName, schedule.name, 7);
                truncName[7] = '\0';
                epd.print(truncName);
            }

            // Separator line
            if (i < visibleCount - 1) {
                epd.drawLine(5, y + 25, SCREEN_WIDTH - 10, y + 25, GxEPD_BLACK);
            }

            y += 30;
        }
    }

    // Scroll indicators and hint
    if (scheduleCount > 3) {
        drawScrollIndicator(scrollOffset, scheduleCount - 3);

        epd.setFont(&FreeSans9pt7b);
        epd.setCursor(5, 122);
        if (scrollMode) {
#if SINGLE_BUTTON_MODE
            epd.print("[Scrolling] Hold to exit");
#else
            epd.print("[Scrolling] Hold PREV to exit");
#endif
        } else {
#if SINGLE_BUTTON_MODE
            epd.print("Hold to scroll");
#else
            epd.print("Hold NEXT to scroll");
#endif
        }
    }
}

void Display::drawConnectivityScreen() {
    drawHeader("Connectivity");

    int y = 28;

    // WiFi section
    epd.setFont(&FreeSansBold12pt7b);
    epd.setCursor(5, y + 14);
    epd.print("WiFi: ");
    epd.setFont(&FreeSans9pt7b);
    epd.print(status.wifiEnabled ? (status.wifiClientConnected ? "Connected" : "On") : "Off");

    if (status.wifiEnabled) {
        y += 22;
        epd.setCursor(10, y + 12);
        epd.print("SSID: ");
        epd.print(status.wifiSSID);

        y += 18;
        epd.setCursor(10, y + 12);
        epd.print("Pass: ");
        epd.print(status.wifiPassword);

        // QR code for WiFi (if enabled)
        if (strlen(status.wifiSSID) > 0) {
            char wifiConfig[96];
            snprintf(wifiConfig, sizeof(wifiConfig), "WIFI:T:WPA;S:%s;P:%s;;",
                     status.wifiSSID, status.wifiPassword);
            drawQRCode(210, 28, wifiConfig, 2);
        }
    }

    // BLE section
    y = 88;
    epd.setFont(&FreeSansBold12pt7b);
    epd.setCursor(5, y + 14);
    epd.print("BLE: ");
    epd.setFont(&FreeSans9pt7b);
    epd.print(status.bleEnabled ? (status.bleClientConnected ? "Connected" : "Advertising") : "Off");

    // Action hint
    epd.setCursor(5, 122);
#if SINGLE_BUTTON_MODE
    epd.print("Hold to toggle WiFi");
#else
    epd.print("Hold NEXT to toggle WiFi");
#endif
}

void Display::drawSettingsScreen() {
    drawHeader("Settings");

    int y = 38;
    epd.setFont(&FreeSans9pt7b);

    Settings& settings = storage.getSettings();

    // Motor duration
    epd.setCursor(5, y);
    epd.print("Motor Duration: ");
    epd.print(settings.motorDuration);
    epd.print(" sec");

    y += 22;
    // Vacation mode
    epd.setCursor(5, y);
    epd.print("Vacation Mode: ");
    epd.print(settings.vacationMode ? "ON" : "OFF");

    y += 22;
    // Battery type
    epd.setCursor(5, y);
    epd.print("Battery Type: ");
    epd.print(settings.getBatteryTypeName());

    y += 22;
    // Time sync status
    epd.setCursor(5, y);
    epd.print("Time Synced: ");
    epd.print(status.timeSynced ? "Yes" : "No");

    // Footer hint
    epd.setCursor(5, 122);
    epd.print("Use WiFi to change settings");
}

void Display::drawAboutScreen() {
    drawHeader("About");

    int y = 42;

    // App name and version
    epd.setFont(&FreeSansBold12pt7b);
    epd.setCursor(5, y);
    epd.print("FeedMe v");
    epd.print(FEEDME_VERSION);

    y += 25;
    epd.setFont(&FreeSans9pt7b);

    // Device ID
    epd.setCursor(5, y);
    epd.print("Device ID: ");
    epd.print(storage.getDeviceId());

    y += 20;
    // Hardware
    epd.setCursor(5, y);
    epd.print("Hardware: ESP32-C3 + E-Paper");

    y += 20;
    // Display
    epd.setCursor(5, y);
    epd.print("Display: 2.9\" 296x128");

    // Footer
    epd.setCursor(5, 122);
    epd.print("Connect to WiFi for setup");
}

// =============================================================================
// Helper Drawing Methods
// =============================================================================

void Display::drawHeader(const char* title) {
    // Header bar
    epd.fillRect(0, 0, SCREEN_WIDTH, 20, GxEPD_BLACK);
    epd.setTextColor(GxEPD_WHITE);
    epd.setFont(&FreeSansBold12pt7b);
    epd.setCursor(5, 16);
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
    if (!enabled) {
        // X mark for disabled
        epd.drawLine(x, y + 2, x + 12, y + 14, GxEPD_WHITE);
        epd.drawLine(x + 12, y + 2, x, y + 14, GxEPD_WHITE);
        return;
    }

    // Dot at bottom for WiFi
    epd.fillCircle(x + 6, y + 13, 2, GxEPD_WHITE);

    // Arcs for signal strength
    if (connected) {
        // Draw arc segments manually since GxEPD2 doesn't have drawArc
        for (int r = 5; r <= 9; r += 4) {
            for (int angle = 225; angle <= 315; angle += 10) {
                float rad = angle * PI / 180.0;
                int px = x + 6 + (int)(r * cos(rad));
                int py = y + 13 + (int)(r * sin(rad));
                epd.drawPixel(px, py, GxEPD_WHITE);
            }
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
    // Warning banner below header
    epd.fillRect(0, 20, SCREEN_WIDTH, 20, GxEPD_BLACK);
    epd.setTextColor(GxEPD_WHITE);
    epd.setFont(&FreeSans9pt7b);

    // Center the text
    int16_t x1, y1;
    uint16_t w, h;
    epd.getTextBounds(message, 0, 0, &x1, &y1, &w, &h);
    epd.setCursor((SCREEN_WIDTH - w) / 2, 35);
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
