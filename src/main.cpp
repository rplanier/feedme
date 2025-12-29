#include <Arduino.h>
#include <esp_sleep.h>
#include <time.h>
#include "config.h"
#include "buttons.h"
#include "display.h"
#include "motor.h"
#include "battery.h"
#include "storage.h"
#include "wifi_manager.h"
#include "webserver.h"

// =============================================================================
// State tracking
// =============================================================================

bool webServerActive = false;
bool wifiStartedBySchedule = false;
bool wifiManuallyEnabled = false;       // Track if user manually enabled WiFi
uint32_t lastActivityTime = 0;          // Last button press or interaction
uint32_t lastStatusUpdate = 0;
uint32_t lastScheduleCheck = 0;
constexpr uint32_t STATUS_UPDATE_INTERVAL = 1000;
constexpr uint32_t SCHEDULE_CHECK_INTERVAL = 10000;  // Check schedules every 10 seconds

// Track which schedules have run today to prevent double-execution
uint8_t lastExecutedScheduleDay = 0;
uint32_t executedScheduleMask = 0;      // Bitmask of schedule IDs executed today

// =============================================================================
// Forward declarations
// =============================================================================

void checkFeedSchedules();
void checkWifiSchedules();
void updateActivityTimer();
void handleSleepTimeout();
void enterDeepSleep();
uint64_t calculateNextWakeTime();

// =============================================================================
// Setup
// =============================================================================

void setup() {
    Serial.begin(115200);
    delay(500);

    // Check wake reason
    esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();

    Serial.println("\n=== FeedMe ===");
    Serial.printf("Wake reason: %d\n", wakeReason);

    // Power enable for peripheral power (T-Display-S3)
    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH);
    delay(100);

    // Initialize storage first (needed for device ID and schedules)
    Serial.println("Initializing storage...");
    if (!storage.begin()) {
        Serial.println("ERROR: Storage initialization failed!");
    }

    // Initialize display
    Serial.println("Initializing display...");
    display.begin();

    // Initialize buttons
    Serial.println("Initializing buttons...");
    buttons.begin();

    // Initialize battery
    Serial.println("Initializing battery...");
    battery.begin();

    // Initialize motor
    Serial.println("Initializing motor...");
    motor.begin();

    // Initialize WiFi manager (but don't start WiFi yet)
    Serial.println("Initializing WiFi...");
    wifiManager.begin(storage.getDeviceId());

    // Pass WiFi credentials to display for Connectivity screen
    display.setPairingInfo(wifiManager.getSSID(), wifiManager.getPassword());

    // Handle wake reason
    switch (wakeReason) {
        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.println("Woke from RTC timer");
            // Check if we woke for a feed schedule
            checkFeedSchedules();
            // Check if we woke for a WiFi window
            checkWifiSchedules();
            // If no user interaction needed, could go back to sleep
            // But for now, stay awake briefly to update display
            break;

        case ESP_SLEEP_WAKEUP_EXT0:
        case ESP_SLEEP_WAKEUP_EXT1:
            Serial.println("Woke from button press");
            // User pressed a button, turn on display and stay awake
            display.setBacklight(true);
            updateActivityTimer();
            break;

        default:
            Serial.println("Cold boot or other wake reason");
            // Normal boot, display stays on
            display.setBacklight(true);
            updateActivityTimer();
            break;
    }

    // Reset executed schedule mask if it's a new day
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {  // 0 = no blocking timeout
        if (timeinfo.tm_mday != lastExecutedScheduleDay) {
            lastExecutedScheduleDay = timeinfo.tm_mday;
            executedScheduleMask = 0;
        }
    }

    Serial.println("Setup complete!");
}

// =============================================================================
// Main loop
// =============================================================================

void loop() {
    uint32_t now = millis();

    // Update subsystems
    buttons.update();
    battery.update();
    motor.update();

    // Check for button events - any button press resets activity timer
    ButtonEvent event = buttons.getEvent();
    if (event != ButtonEvent::NONE) {
        updateActivityTimer();

        // If display was off, just turn it on (consume the button press)
        if (!display.isBacklightOn()) {
            display.setBacklight(true);
        } else {
            // Display is on, handle the button normally
            display.handleButton(event);
        }
    }

    // Check schedules periodically
    if (now - lastScheduleCheck >= SCHEDULE_CHECK_INTERVAL) {
        lastScheduleCheck = now;
        checkFeedSchedules();
        checkWifiSchedules();
    }

    // Handle WiFi state
    if (wifiManager.isRunning()) {
        wifiManager.update();

        // Start webserver if WiFi is running but server isn't
        if (!webServerActive) {
            webServer.begin();
            webServerActive = true;
            Serial.println("WebServer started");
        }

        // Check for auto-stop due to idle timeout (only for manually started WiFi)
        if (wifiManuallyEnabled && !wifiStartedBySchedule &&
            wifiManager.shouldAutoStop() && wifiManager.getClientCount() == 0) {
            Serial.println("WiFi idle timeout - stopping");
            wifiManager.stop();
            wifiManuallyEnabled = false;
        }
    } else if (webServerActive) {
        webServer.stop();
        webServerActive = false;
        wifiStartedBySchedule = false;
        Serial.println("WebServer stopped");
    }

    // Update display status periodically
    if (now - lastStatusUpdate >= STATUS_UPDATE_INTERVAL) {
        lastStatusUpdate = now;

        StatusData status;
        status.batteryVoltage = battery.getVoltage();
        status.isCharging = battery.isCharging();
        status.batteryStatus = battery.getStatusText();
        status.wifiConnected = wifiManager.isRunning();
        status.vacationMode = storage.getSettings().vacationMode;
        display.setStatus(status);
    }

    // Update display (only if backlight is on)
    if (display.isBacklightOn()) {
        display.update();
    }

    // Handle sleep timeout
    handleSleepTimeout();

    delay(10);
}

// =============================================================================
// Activity tracking
// =============================================================================

void updateActivityTimer() {
    lastActivityTime = millis();
}

// =============================================================================
// Sleep timeout handling
// =============================================================================

void handleSleepTimeout() {
    Settings& settings = storage.getSettings();
    uint16_t timeoutSeconds = settings.getSleepTimeoutSeconds();

    // If timeout is disabled (0), never sleep
    if (timeoutSeconds == 0) {
        return;
    }

    uint32_t now = millis();
    uint32_t inactiveMs = now - lastActivityTime;
    uint32_t timeoutMs = timeoutSeconds * 1000UL;

    // Check if we've exceeded the timeout
    if (inactiveMs >= timeoutMs) {
        // Don't sleep if motor is running
        if (motor.isRunning()) {
            return;
        }

        // Don't sleep if WiFi has connected clients
        if (wifiManager.isRunning() && wifiManager.getClientCount() > 0) {
            return;
        }

        // Turn off display first
        if (display.isBacklightOn()) {
            Serial.println("Display timeout - turning off backlight");
            display.setBacklight(false);
        }

        // If WiFi is not running and no motor activity, enter deep sleep
        if (!wifiManager.isRunning()) {
            enterDeepSleep();
        }
    }
}

// =============================================================================
// Deep sleep
// =============================================================================

void enterDeepSleep() {
    Serial.println("Entering deep sleep...");

    // Calculate next wake time
    uint64_t sleepTimeUs = calculateNextWakeTime();

    if (sleepTimeUs > 0) {
        Serial.printf("Will wake in %llu seconds\n", sleepTimeUs / 1000000ULL);
        esp_sleep_enable_timer_wakeup(sleepTimeUs);
    } else {
        Serial.println("No scheduled wake time, will wake on button only");
    }

    // Configure GPIO wake on either button (active low)
    // Use ext1 for multiple GPIOs - wake on any LOW
    uint64_t buttonMask = (1ULL << PIN_BUTTON_TOP) | (1ULL << PIN_BUTTON_BOTTOM);
    esp_sleep_enable_ext1_wakeup(buttonMask, ESP_EXT1_WAKEUP_ANY_LOW);

    // Flush serial before sleep
    Serial.flush();

    // Enter deep sleep
    esp_deep_sleep_start();

    // This line is never reached
}

uint64_t calculateNextWakeTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 0)) {  // 0 = no blocking timeout
        // No valid time, can't calculate wake time
        return 0;
    }

    int currentHour = timeinfo.tm_hour;
    int currentMinute = timeinfo.tm_min;
    int currentSecond = timeinfo.tm_sec;
    int currentDay = timeinfo.tm_wday;

    uint32_t currentDaySeconds = currentHour * 3600 + currentMinute * 60 + currentSecond;
    uint32_t nearestWakeSeconds = UINT32_MAX;

    // Check feed schedules
    int scheduleCount = storage.getScheduleCount();
    for (int i = 0; i < scheduleCount; i++) {
        Schedule* sched = storage.getSchedule(i);
        if (!sched || !sched->enabled) continue;
        if (storage.getSettings().vacationMode) continue;

        // Check if schedule is active today
        if (!sched->isActiveOnDay(currentDay)) continue;

        // Check seasonal dates
        if (!sched->isActiveOnDate(timeinfo.tm_mon + 1, timeinfo.tm_mday)) continue;

        uint32_t schedSeconds = sched->hour * 3600 + sched->minute * 60;

        // If schedule is in the future today
        if (schedSeconds > currentDaySeconds) {
            uint32_t secondsUntil = schedSeconds - currentDaySeconds;
            if (secondsUntil < nearestWakeSeconds) {
                nearestWakeSeconds = secondsUntil;
            }
        }
    }

    // Check WiFi schedules for start times
    int wifiCount = storage.getWifiScheduleCount();
    for (int i = 0; i < wifiCount; i++) {
        WifiSchedule* sched = storage.getWifiSchedule(i);
        if (!sched || !sched->enabled) continue;
        if (!sched->isActiveOnDay(currentDay)) continue;

        uint32_t startSeconds = sched->startHour * 3600 + sched->startMinute * 60;

        if (startSeconds > currentDaySeconds) {
            uint32_t secondsUntil = startSeconds - currentDaySeconds;
            if (secondsUntil < nearestWakeSeconds) {
                nearestWakeSeconds = secondsUntil;
            }
        }
    }

    // If no wake time found today, check for tomorrow's first schedule
    if (nearestWakeSeconds == UINT32_MAX) {
        // Calculate seconds until midnight, then add first schedule of tomorrow
        uint32_t secondsUntilMidnight = 86400 - currentDaySeconds;
        int tomorrowDay = (currentDay + 1) % 7;

        uint32_t earliestTomorrow = UINT32_MAX;

        // Check feed schedules for tomorrow
        for (int i = 0; i < scheduleCount; i++) {
            Schedule* sched = storage.getSchedule(i);
            if (!sched || !sched->enabled) continue;
            if (storage.getSettings().vacationMode) continue;
            if (!sched->isActiveOnDay(tomorrowDay)) continue;

            uint32_t schedSeconds = sched->hour * 3600 + sched->minute * 60;
            if (schedSeconds < earliestTomorrow) {
                earliestTomorrow = schedSeconds;
            }
        }

        // Check WiFi schedules for tomorrow
        for (int i = 0; i < wifiCount; i++) {
            WifiSchedule* sched = storage.getWifiSchedule(i);
            if (!sched || !sched->enabled) continue;
            if (!sched->isActiveOnDay(tomorrowDay)) continue;

            uint32_t startSeconds = sched->startHour * 3600 + sched->startMinute * 60;
            if (startSeconds < earliestTomorrow) {
                earliestTomorrow = startSeconds;
            }
        }

        if (earliestTomorrow < UINT32_MAX) {
            nearestWakeSeconds = secondsUntilMidnight + earliestTomorrow;
        }
    }

    if (nearestWakeSeconds == UINT32_MAX) {
        return 0;  // No scheduled wake
    }

    // Convert to microseconds
    return (uint64_t)nearestWakeSeconds * 1000000ULL;
}

// =============================================================================
// Feed schedule checking
// =============================================================================

void checkFeedSchedules() {
    if (storage.getSettings().vacationMode) {
        return;  // Vacation mode - no feeding
    }

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 0)) {  // 0 = no blocking timeout
        return;  // No valid time
    }

    // Reset executed mask if it's a new day
    if (timeinfo.tm_mday != lastExecutedScheduleDay) {
        lastExecutedScheduleDay = timeinfo.tm_mday;
        executedScheduleMask = 0;
    }

    int currentHour = timeinfo.tm_hour;
    int currentMinute = timeinfo.tm_min;
    int currentDay = timeinfo.tm_wday;
    int currentMonth = timeinfo.tm_mon + 1;
    int currentDayOfMonth = timeinfo.tm_mday;

    int scheduleCount = storage.getScheduleCount();
    for (int i = 0; i < scheduleCount; i++) {
        Schedule* sched = storage.getSchedule(i);
        if (!sched || !sched->enabled) continue;

        // Check if already executed today
        if (executedScheduleMask & (1U << sched->id)) continue;

        // Check day of week
        if (!sched->isActiveOnDay(currentDay)) continue;

        // Check seasonal dates
        if (!sched->isActiveOnDate(currentMonth, currentDayOfMonth)) continue;

        // Check if it's time (within the check interval window)
        if (sched->hour == currentHour && sched->minute == currentMinute) {
            Serial.printf("Executing feed schedule: %s\n", sched->name);

            // Mark as executed
            executedScheduleMask |= (1U << sched->id);

            // Check battery before running motor
            if (battery.getStatus() == BatteryStatus::CRITICAL) {
                Serial.println("Battery critical - skipping feed");
                continue;
            }

            // Run motor with schedule-specific or default duration
            uint8_t duration = sched->duration > 0 ? sched->duration : motor.getDefaultDuration();
            motor.startThrow(duration);

            // Keep display/activity alive during motor run
            updateActivityTimer();
        }
    }
}

// =============================================================================
// WiFi schedule checking
// =============================================================================

void checkWifiSchedules() {
    bool shouldBeActive = storage.shouldWifiBeActive();
    bool hasClients = wifiManager.getClientCount() > 0;

    if (shouldBeActive && !wifiManager.isRunning()) {
        Serial.println("WiFi schedule active - starting WiFi");
        wifiManager.start();
        wifiStartedBySchedule = true;
        updateActivityTimer();  // Reset activity timer when WiFi starts
    } else if (!shouldBeActive && wifiManager.isRunning() && wifiStartedBySchedule && !hasClients) {
        Serial.println("WiFi schedule ended - stopping WiFi");
        wifiManager.stop();
        wifiStartedBySchedule = false;
    }
}
