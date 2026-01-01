#include <Arduino.h>
#include <esp_sleep.h>
#include "config.h"
#include "buttons.h"
#include "display.h"
#include "motor.h"
#include "battery.h"
#include "storage.h"
#include "wifi_manager.h"
#include "webserver.h"
#include "ble_manager.h"
#include "rtc_manager.h"

// =============================================================================
// State tracking
// =============================================================================

bool webServerActive = false;
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
void checkBleSchedules();
void updateActivityTimer();
void handleSleepTimeout();
void enterDeepSleep();
uint64_t calculateNextWakeTime();
void formatNextFeedTime(char* buffer, size_t len);

// =============================================================================
// Setup
// =============================================================================

void setup() {
    Serial.begin(115200);
    delay(500);

    // Check wake reason
    esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();

    Serial.println("\n=== FeedMe ===");
    Serial.printf("Version: %s\n", FEEDME_VERSION);
    Serial.printf("Wake reason: %d\n", wakeReason);

    // Initialize storage first (needed for device ID and schedules)
    Serial.println("Initializing storage...");
    if (!storage.begin()) {
        Serial.println("ERROR: Storage initialization failed!");
    }

    // Initialize RTC manager
    Serial.println("Initializing RTC...");
    rtcManager.begin();

    // Initialize display (e-ink, no backlight needed)
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

    // Initialize BLE manager
    Serial.println("Initializing BLE...");
    bleManager.begin(storage.getDeviceId());

    // Handle wake reason
    switch (wakeReason) {
        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.println("Woke from RTC timer");
            // Check if we woke for a feed schedule (only if time is synced)
            if (rtcManager.isTimeSynced()) {
                checkFeedSchedules();
            }
            // Check if we woke for a BLE window
            checkBleSchedules();
            break;

        case ESP_SLEEP_WAKEUP_EXT0:
        case ESP_SLEEP_WAKEUP_EXT1:
            Serial.println("Woke from button press");
            updateActivityTimer();
            break;

        default:
            Serial.println("Cold boot or other wake reason");
            updateActivityTimer();
            break;
    }

    // Reset executed schedule mask if it's a new day
    DateTime now = rtcManager.now();
    if (now.day() != lastExecutedScheduleDay) {
        lastExecutedScheduleDay = now.day();
        executedScheduleMask = 0;
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
        display.handleButton(event);
    }

    // Check for WiFi toggle request from display (user held button on Connectivity screen)
    if (display.shouldToggleWifi()) {
        display.clearWifiToggleRequest();
        if (wifiManager.isRunning()) {
            Serial.println("User requested WiFi stop");
            wifiManager.stop();
            // Immediately restart BLE if it should be active
            checkBleSchedules();
        } else {
            Serial.println("User requested WiFi start");
            wifiManager.start();
        }
        display.invalidate();  // Force display update
    }

    // Check schedules periodically (only if time is synced)
    if (now - lastScheduleCheck >= SCHEDULE_CHECK_INTERVAL) {
        lastScheduleCheck = now;
        if (rtcManager.isTimeSynced()) {
            checkFeedSchedules();
        }
        checkBleSchedules();
    }

    // Check for BLE wake request (user connected via BLE and sent wake command)
    if (bleManager.hasWakeRequest()) {
        Serial.println("Main loop: BLE wake request detected");
        bleManager.clearWakeRequest();
        if (!wifiManager.isRunning()) {
            Serial.println("BLE wake request - starting WiFi");
            // Fully deinit BLE before starting WiFi (release radio)
            bleManager.deinit();
            delay(500);  // Let radio fully settle
            wifiManager.start();
            updateActivityTimer();
        } else {
            Serial.println("WiFi already running, ignoring wake request");
        }
    }

    // Update BLE manager
    bleManager.update();

    // Handle WiFi state
    if (wifiManager.isRunning()) {
        wifiManager.update();

        // Start webserver if WiFi is running but server isn't
        if (!webServerActive) {
            webServer.begin();
            webServer.setThrowCallback([]() {
                motor.startThrow();  // Uses default duration
            });
            webServerActive = true;
            Serial.println("WebServer started");
        }

        // Auto-stop WiFi after 5 minutes of inactivity (no API requests)
        if (wifiManager.shouldAutoStop()) {
            Serial.println("WiFi idle timeout - stopping");
            wifiManager.stop();
            // Immediately restart BLE if it should be active
            checkBleSchedules();
        }
    } else if (webServerActive) {
        webServer.stop();
        webServerActive = false;
        Serial.println("WebServer stopped");
        // Immediately restart BLE if it should be active
        checkBleSchedules();
    }

    // Update display status periodically
    if (now - lastStatusUpdate >= STATUS_UPDATE_INTERVAL) {
        lastStatusUpdate = now;

        StatusData status = {};

        // Time
        rtcManager.formatTime(status.currentTime, sizeof(status.currentTime));
        rtcManager.formatDate(status.currentDate, sizeof(status.currentDate));

        // Battery
        status.batteryVoltage = battery.getVoltage();
        status.isCharging = battery.isCharging();
        status.batteryStatus = battery.getStatusText();

        // Connectivity
        status.wifiEnabled = wifiManager.isRunning();
        status.wifiClientConnected = wifiManager.getClientCount() > 0;
        status.bleEnabled = bleManager.isRunning();
        status.bleClientConnected = bleManager.isClientConnected();

        // Status flags
        status.vacationMode = storage.getSettings().vacationMode;
        status.timeSynced = rtcManager.isTimeSynced();

        // WiFi credentials for connectivity screen
        strncpy(status.wifiSSID, wifiManager.getSSID(), sizeof(status.wifiSSID) - 1);
        strncpy(status.wifiPassword, wifiManager.getPassword(), sizeof(status.wifiPassword) - 1);

        // Next feed time
        formatNextFeedTime(status.nextFeedTime, sizeof(status.nextFeedTime));

        display.setStatus(status);

    }

    // Update display (e-ink updates only when needed)
    display.update();

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
    // Don't sleep if motor is running
    if (motor.isRunning()) {
        return;
    }

    // Don't sleep if WiFi is running (has its own idle timeout)
    if (wifiManager.isRunning()) {
        return;
    }

    // Don't sleep if BLE is running - stay awake to receive connections
    if (bleManager.isRunning()) {
        return;
    }

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
        // E-ink display persists without power - enter deep sleep
        enterDeepSleep();
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

    // Configure GPIO wake on either button (active low with pull-up)
    // ESP32-C3 uses different deep sleep wake API
    #if defined(CONFIG_IDF_TARGET_ESP32C3)
    // ESP32-C3: use GPIO wake (any high level triggers wake when using pull-up + active low)
    esp_deep_sleep_enable_gpio_wakeup(
        (1ULL << PIN_BUTTON_PREV) | (1ULL << PIN_BUTTON_NEXT),
        ESP_GPIO_WAKEUP_GPIO_LOW
    );
    #else
    // ESP32-S3 and other variants: use ext1
    uint64_t buttonMask = (1ULL << PIN_BUTTON_PREV) | (1ULL << PIN_BUTTON_NEXT);
    esp_sleep_enable_ext1_wakeup(buttonMask, ESP_EXT1_WAKEUP_ANY_LOW);
    #endif

    // Flush serial before sleep
    Serial.flush();

    // Enter deep sleep
    esp_deep_sleep_start();

    // This line is never reached
}

uint64_t calculateNextWakeTime() {
    // If time not synced, don't calculate feed wake times
    if (!rtcManager.isTimeSynced()) {
        // Still check BLE schedules though (they work without synced time)
        // For now, return 0 to rely on button wake only
        return 0;
    }

    DateTime now = rtcManager.now();
    int currentHour = now.hour();
    int currentMinute = now.minute();
    int currentSecond = now.second();
    int currentDay = now.dayOfTheWeek();

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
        if (!sched->isActiveOnDate(now.month(), now.day())) continue;

        uint32_t schedSeconds = sched->hour * 3600 + sched->minute * 60;

        // If schedule is in the future today
        if (schedSeconds > currentDaySeconds) {
            uint32_t secondsUntil = schedSeconds - currentDaySeconds;
            if (secondsUntil < nearestWakeSeconds) {
                nearestWakeSeconds = secondsUntil;
            }
        }
    }

    // Check BLE schedules for start times
    int bleCount = storage.getBleScheduleCount();
    for (int i = 0; i < bleCount; i++) {
        BleSchedule* sched = storage.getBleSchedule(i);
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

        // Check BLE schedules for tomorrow
        for (int i = 0; i < bleCount; i++) {
            BleSchedule* sched = storage.getBleSchedule(i);
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

    // Only check schedules if time is synced
    if (!rtcManager.isTimeSynced()) {
        return;
    }

    DateTime now = rtcManager.now();

    // Reset executed mask if it's a new day
    if (now.day() != lastExecutedScheduleDay) {
        lastExecutedScheduleDay = now.day();
        executedScheduleMask = 0;
    }

    int currentHour = now.hour();
    int currentMinute = now.minute();
    int currentDay = now.dayOfTheWeek();
    int currentMonth = now.month();
    int currentDayOfMonth = now.day();

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
// BLE schedule checking
// =============================================================================

void checkBleSchedules() {
    bool shouldBeActive = storage.shouldBleBeActive();

    // Don't start BLE while WiFi is running (radio coexistence)
    if (shouldBeActive && !bleManager.isRunning() && !wifiManager.isRunning()) {
        Serial.println("BLE schedule active - starting BLE");
        bleManager.start();
    } else if (!shouldBeActive && bleManager.isRunning()) {
        Serial.println("BLE schedule inactive - stopping BLE");
        bleManager.stop();
    }
}

// =============================================================================
// Next feed time formatting
// =============================================================================

void formatNextFeedTime(char* buffer, size_t len) {
    static const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    // Default to "None" if no valid schedule found
    strncpy(buffer, "None", len);

    if (!rtcManager.isTimeSynced() || storage.getSettings().vacationMode) {
        return;
    }

    DateTime now = rtcManager.now();
    int currentHour = now.hour();
    int currentMinute = now.minute();
    int currentDay = now.dayOfTheWeek();
    uint32_t currentDaySeconds = currentHour * 3600 + currentMinute * 60;

    uint32_t nearestSeconds = UINT32_MAX;
    int nearestDay = -1;
    int nearestHour = 0;
    int nearestMinute = 0;

    // Check schedules for today and the next 7 days
    for (int dayOffset = 0; dayOffset < 7; dayOffset++) {
        int checkDay = (currentDay + dayOffset) % 7;
        uint32_t baseSeconds = dayOffset * 86400;

        int scheduleCount = storage.getScheduleCount();
        for (int i = 0; i < scheduleCount; i++) {
            Schedule* sched = storage.getSchedule(i);
            if (!sched || !sched->enabled) continue;
            if (!sched->isActiveOnDay(checkDay)) continue;
            if (!sched->isActiveOnDate(now.month(), now.day())) continue;

            uint32_t schedSeconds = sched->hour * 3600 + sched->minute * 60;

            // Skip if this is today and the time has passed
            if (dayOffset == 0 && schedSeconds <= currentDaySeconds) {
                continue;
            }

            uint32_t totalSeconds = baseSeconds + schedSeconds - currentDaySeconds;
            if (totalSeconds < nearestSeconds) {
                nearestSeconds = totalSeconds;
                nearestDay = checkDay;
                nearestHour = sched->hour;
                nearestMinute = sched->minute;
            }
        }
    }

    if (nearestDay >= 0) {
        // Format as "Today HH:MM" or "Mon HH:MM"
        if (nearestDay == currentDay && nearestSeconds < 86400) {
            snprintf(buffer, len, "Today %02d:%02d", nearestHour, nearestMinute);
        } else {
            snprintf(buffer, len, "%s %02d:%02d", days[nearestDay], nearestHour, nearestMinute);
        }
    }
}
