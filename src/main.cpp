#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "buttons.h"
#include "display.h"
#include "motor.h"
#include "battery.h"
#include "storage.h"
#include "radio_manager.h"
#include "rtc_manager.h"

// =============================================================================
// State tracking
// =============================================================================
uint32_t lastActivityTime = 0;          // Last button press or interaction
uint32_t lastStatusUpdate = 0;
uint32_t lastScheduleCheck = 0;
constexpr uint32_t STATUS_UPDATE_INTERVAL = 1000;
constexpr uint32_t SCHEDULE_CHECK_INTERVAL = 10000;  // Check schedules every 10 seconds

// Track which schedules have run today to prevent double-execution
uint8_t lastExecutedScheduleDay = 0;
uint32_t executedScheduleMask = 0;      // Bitmask of schedule IDs executed today

// Last reset reason (stored at boot for debugging)
static esp_reset_reason_t lastResetReason = ESP_RST_UNKNOWN;

const char* getResetReasonString() {
    switch (lastResetReason) {
        case ESP_RST_POWERON:   return "Power-on";
        case ESP_RST_EXT:       return "External reset";
        case ESP_RST_SW:        return "Software reset";
        case ESP_RST_PANIC:     return "Exception/panic";
        case ESP_RST_INT_WDT:   return "Interrupt watchdog";
        case ESP_RST_TASK_WDT:  return "Task watchdog";
        case ESP_RST_WDT:       return "Other watchdog";
        case ESP_RST_DEEPSLEEP: return "Deep sleep wake";
        case ESP_RST_BROWNOUT:  return "Brownout";
        case ESP_RST_SDIO:      return "SDIO reset";
        default:                return "Unknown";
    }
}

// =============================================================================
// Forward declarations
// =============================================================================

void checkFeedSchedules();
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
    delay(2000);  // Wait for USB serial to be ready
    Serial.println("\n\n*** BOOT START ***");
    Serial.flush();

    // Capture reset reason for debugging (before any other init that might change it)
    lastResetReason = esp_reset_reason();

    // Check wake reason
    esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();

    Serial.println("\n=== FeedMe ===");
    Serial.printf("Version: %s\n", FEEDME_VERSION);
    Serial.printf("Reset reason: %s\n", getResetReasonString());
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

    // Initialize radio manager (handles both WiFi and BLE)
    Serial.println("Initializing radio...");
    radioManager.begin(storage.getDeviceId());
    radioManager.setThrowCallback([]() {
        motor.startThrow();
    });

    // Handle wake reason
    switch (wakeReason) {
        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.println("Woke from RTC timer");
            // Check if we woke for a feed schedule (only if time is synced)
            if (rtcManager.isTimeSynced()) {
                checkFeedSchedules();
            }
            // Check if we woke for a BLE window
            radioManager.checkBleSchedules();
            break;

        case ESP_SLEEP_WAKEUP_EXT0:
        case ESP_SLEEP_WAKEUP_EXT1:
            Serial.println("Woke from button press");
            updateActivityTimer();
            break;

        default:
            Serial.println("Cold boot or other wake reason");
            updateActivityTimer();
            // Auto-enable WiFi on cold boot for easier initial setup
            // The 5-minute idle timeout will shut it off automatically
            Serial.println("Auto-starting WiFi for initial setup...");
            radioManager.transitionToWifi();
            break;
    }

    // Reset executed schedule mask if it's a new day
    DateTime now = rtcManager.now();
    if (now.day() != lastExecutedScheduleDay) {
        lastExecutedScheduleDay = now.day();
        executedScheduleMask = 0;
    }

    // Initialize watchdog timer - reset device if main loop hangs
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WATCHDOG_TIMEOUT_SEC * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(NULL);  // Add current task to watchdog
    Serial.printf("Watchdog initialized (%lu sec timeout)\n", WATCHDOG_TIMEOUT_SEC);

    Serial.println("Setup complete!");
}

// =============================================================================
// Main loop
// =============================================================================

void loop() {
    // Feed watchdog at start of each loop iteration
    esp_task_wdt_reset();

    uint32_t now = millis();

    // Update subsystems
    buttons.update();
    battery.update();
    motor.update();

    // Drain ALL pending button events before refreshing display
    // This allows rapid presses during e-paper refresh to skip intermediate screens
    while (true) {
        ButtonEvent event = buttons.getEvent();
        if (event == ButtonEvent::NONE) break;

        updateActivityTimer();
        // Debug: log button events
        if (event == ButtonEvent::NEXT_PRESS) {
            Serial.println("Button: NEXT_PRESS");
        } else if (event == ButtonEvent::NEXT_HOLD) {
            Serial.printf("Button: NEXT_HOLD (screen=%d)\n", static_cast<int>(display.getScreen()));
        } else if (event == ButtonEvent::PREV_PRESS) {
            Serial.println("Button: PREV_PRESS");
        } else if (event == ButtonEvent::PREV_HOLD) {
            Serial.println("Button: PREV_HOLD");
        }
        display.handleButton(event);
    }

    // Check for WiFi toggle request from display (user held button on Connectivity screen)
    if (display.shouldToggleWifi()) {
        display.clearWifiToggleRequest();
        if (radioManager.isWifiActive()) {
            Serial.println("User requested WiFi stop");
            radioManager.transitionToBle();
        } else {
            Serial.println("User requested WiFi start");
            radioManager.transitionToWifi();
        }
        // Force immediate status update so display shows new WiFi state
        lastStatusUpdate = 0;
    }

    // Check for manual feed request from display (user held button on Overview screen)
    // Manual feed always works - user is explicitly overriding any battery restrictions
    if (display.shouldStartFeedCountdown()) {
        display.clearFeedCountdownRequest();

        Serial.println("Manual feed countdown starting...");

        // Show initial warning on display
        esp_task_wdt_reset();  // Reset before blocking display operation
        display.showFeedCountdown(MANUAL_FEED_COUNTDOWN_SEC);
        esp_task_wdt_reset();  // Reset after display operation

        // Countdown with periodic display updates
        bool cancelled = false;
        for (int i = MANUAL_FEED_COUNTDOWN_SEC; i > 0 && !cancelled; i--) {
            Serial.printf("Feed in %d...\n", i);
            esp_task_wdt_reset();  // Keep watchdog happy

            // Update display at key intervals (10s, 5s, 3s)
            if (i == 10 || i == 5 || i == 3) {
                display.showFeedCountdown(i);
                esp_task_wdt_reset();  // Reset after display operation
            }

            // Check for button press to cancel (poll for 1 second)
            uint32_t start = millis();
            while (millis() - start < 1000) {
                buttons.update();
                ButtonEvent event = buttons.getEvent();
                if (event == ButtonEvent::NEXT_PRESS || event == ButtonEvent::PREV_PRESS) {
                    cancelled = true;
                    Serial.println("Feed cancelled by button press");
                    break;
                }
                delay(10);
            }
        }

        if (cancelled) {
            esp_task_wdt_reset();  // Reset before blocking display operation
            display.showFeedCancelled();
            esp_task_wdt_reset();  // Reset after display operation
            // Brief delay to show message, with watchdog resets
            for (int i = 0; i < 15; i++) {
                delay(100);
                esp_task_wdt_reset();
            }
        } else {
            // Show feeding now warning
            esp_task_wdt_reset();  // Reset before blocking display operation
            display.showFeedingNow();
            esp_task_wdt_reset();  // Reset after display operation

            Serial.printf("Feeding now! (%d seconds)\n", MANUAL_FEED_DURATION_SEC);
            motor.startThrow(MANUAL_FEED_DURATION_SEC);

            // Log manual feed event to history
            storage.logFeedEvent(MANUAL_FEED_DURATION_SEC, true, "");
        }

        // Update activity timer and force display refresh
        updateActivityTimer();
        display.forceFullRefresh();
    }

    // Check schedules periodically (only if time is synced)
    if (now - lastScheduleCheck >= SCHEDULE_CHECK_INTERVAL) {
        lastScheduleCheck = now;
        if (rtcManager.isTimeSynced()) {
            checkFeedSchedules();
        }
        radioManager.checkBleSchedules();
    }

    // Update radio manager (handles BLE wake requests, WiFi idle timeout, etc.)
    radioManager.update();

    // Update display status periodically, or immediately if requested (e.g., after settings change)
    if (now - lastStatusUpdate >= STATUS_UPDATE_INTERVAL || display.needsStatusUpdate()) {
        lastStatusUpdate = now;
        display.clearStatusUpdateFlag();

        StatusData status = {};

        // Time (convert UTC to local for display)
        int16_t tzOffset = storage.getSettings().timezoneOffset;
        rtcManager.formatTimeLocal(status.currentTime, sizeof(status.currentTime), tzOffset);
        rtcManager.formatDateLocal(status.currentDate, sizeof(status.currentDate), tzOffset);

        // Battery
        status.batteryVoltage = battery.getVoltage();
        status.isCharging = battery.isCharging();
        status.batteryStatus = battery.getStatusText();

        // Connectivity
        status.wifiEnabled = radioManager.isWifiActive();
        status.wifiClientConnected = radioManager.isWifiClientConnected();
        status.bleEnabled = radioManager.isBleActive();
        status.bleClientConnected = radioManager.isBleClientConnected();

        // Status flags
        status.vacationMode = storage.getSettings().vacationMode;
        status.timeSynced = rtcManager.isTimeSynced();

        // WiFi credentials for connectivity screen
        strncpy(status.wifiSSID, radioManager.getWifiSSID(), sizeof(status.wifiSSID) - 1);
        strncpy(status.wifiPassword, radioManager.getWifiPassword(), sizeof(status.wifiPassword) - 1);

        // Next feed time
        formatNextFeedTime(status.nextFeedTime, sizeof(status.nextFeedTime));

        display.setStatus(status);

    }

    // Update display (e-ink updates only when needed)
    display.update();

    // Check if we should enter screensaver mode (after 60s of button inactivity)
    display.checkScreensaver();

    // Process any button events captured during display refresh immediately
    buttons.update();
    while (buttons.hasEvent()) {
        ButtonEvent event = buttons.getEvent();
        if (event == ButtonEvent::NONE) break;
        updateActivityTimer();
        display.handleButton(event);
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
    // Don't sleep if motor is running
    if (motor.isRunning()) {
        return;
    }

    // Don't sleep if WiFi or BLE is running
    if (radioManager.getMode() != RadioManager::Mode::IDLE) {
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

            // Log feed event to history
            storage.logFeedEvent(duration, false, sched->name);

            // Keep display/activity alive during motor run
            updateActivityTimer();
        }
    }
}

// =============================================================================
// Next feed time formatting
// =============================================================================

void formatNextFeedTime(char* buffer, size_t len) {
    // If battery is critical, scheduled feeds are skipped - show this instead of a time
    if (battery.getStatus() == BatteryStatus::CRITICAL) {
        strncpy(buffer, "Battery Low", len);
        return;
    }

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
        // Convert UTC hour to local time for display
        int16_t tzOffset = storage.getSettings().timezoneOffset;
        int localHour = nearestHour - (tzOffset / 60);
        localHour = (localHour + 24) % 24;

        // Format as "Today HH:MM" or "Mon HH:MM"
        if (nearestDay == currentDay && nearestSeconds < 86400) {
            snprintf(buffer, len, "Today %02d:%02d", localHour, nearestMinute);
        } else {
            snprintf(buffer, len, "%s %02d:%02d", DAY_NAMES[nearestDay], localHour, nearestMinute);
        }
    }
}
