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
#include "sun_calc.h"
#include "time_format.h"

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

// Feed overlap protection - minimum time between feeds (milliseconds)
constexpr uint32_t MIN_FEED_INTERVAL_MS = 60000;  // 60 seconds
uint32_t lastFeedCompletedTime = 0;               // millis() when last feed finished

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
#if SERIAL_DEBUG
    Serial.begin(115200);
    delay(2000);  // Wait for USB serial to be ready
    DEBUG_PRINTLN("\n\n*** BOOT START ***");
    Serial.flush();
#endif

    // Capture reset reason for debugging (before any other init that might change it)
    lastResetReason = esp_reset_reason();

    // Check wake reason
    esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();

    DEBUG_PRINTLN("\n=== FeedMe ===");
    DEBUG_PRINTF("Version: %s\n", FEEDME_VERSION);
    DEBUG_PRINTF("Reset reason: %s\n", getResetReasonString());
    DEBUG_PRINTF("Wake reason: %d\n", wakeReason);

    // Initialize storage first (needed for device ID and schedules)
    DEBUG_PRINTLN("Initializing storage...");
    if (!storage.begin()) {
        DEBUG_PRINTLN("ERROR: Storage initialization failed!");
    }

    // Apply timezone settings (enables localtime() to handle DST automatically)
    storage.applyTimezone();

    // Initialize RTC manager
    DEBUG_PRINTLN("Initializing RTC...");
    rtcManager.begin();

    // Initialize display (e-ink, no backlight needed)
    DEBUG_PRINTLN("Initializing display...");
    display.begin();

    // Initialize buttons
    DEBUG_PRINTLN("Initializing buttons...");
    buttons.begin();

    // Initialize battery
    DEBUG_PRINTLN("Initializing battery...");
    battery.begin();

    // Initialize motor
    DEBUG_PRINTLN("Initializing motor...");
    motor.begin();

    // Initialize radio manager (handles both WiFi and BLE)
    DEBUG_PRINTLN("Initializing radio...");
    radioManager.begin(storage.getDeviceId());

    // Handle wake reason
    switch (wakeReason) {
        case ESP_SLEEP_WAKEUP_TIMER:
            DEBUG_PRINTLN("Woke from RTC timer");
            // Check if we woke for a feed schedule (only if time is synced)
            if (rtcManager.isTimeSynced()) {
                checkFeedSchedules();
            }
            // Check if we woke for a BLE window
            radioManager.checkBleSchedules();
            break;

        case ESP_SLEEP_WAKEUP_EXT0:
        case ESP_SLEEP_WAKEUP_EXT1:
            DEBUG_PRINTLN("Woke from button press");
            updateActivityTimer();
            break;

        default:
            DEBUG_PRINTLN("Cold boot or other wake reason");
            updateActivityTimer();
            // Start in BLE mode (lower power than WiFi)
            // If BLE schedules exist, allow 5-minute grace period after boot
            DEBUG_PRINTLN("Starting BLE...");
            radioManager.transitionToBle();
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
    DEBUG_PRINTF("Watchdog initialized (%lu sec timeout)\n", WATCHDOG_TIMEOUT_SEC);

    DEBUG_PRINTLN("Setup complete!");
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

    // Track motor state to detect when feed completes
    bool wasMotorRunning = motor.isRunning();
    motor.update();

    // Update last feed completion time when motor stops
    if (wasMotorRunning && !motor.isRunning()) {
        lastFeedCompletedTime = millis();
        DEBUG_PRINTLN("Feed completed - updating lastFeedCompletedTime");
    }

    // Drain ALL pending button events before refreshing display
    // This allows rapid presses during e-paper refresh to skip intermediate screens
    while (true) {
        ButtonEvent event = buttons.getEvent();
        if (event == ButtonEvent::NONE) break;

        updateActivityTimer();
        // Debug: log button events
        if (event == ButtonEvent::NEXT_PRESS) {
            DEBUG_PRINTLN("Button: NEXT_PRESS");
        } else if (event == ButtonEvent::NEXT_HOLD) {
            DEBUG_PRINTF("Button: NEXT_HOLD (screen=%d)\n", static_cast<int>(display.getScreen()));
        } else if (event == ButtonEvent::PREV_PRESS) {
            DEBUG_PRINTLN("Button: PREV_PRESS");
        } else if (event == ButtonEvent::PREV_HOLD) {
            DEBUG_PRINTLN("Button: PREV_HOLD");
        }
        display.handleButton(event);
    }

    // Check for manual feed request from display (user held button on Overview screen)
    // Manual feed always works - user is explicitly overriding any battery restrictions
    if (display.shouldStartFeedCountdown()) {
        display.clearFeedCountdownRequest();

        DEBUG_PRINTLN("Manual feed countdown starting...");

        // Show initial warning on display
        esp_task_wdt_reset();  // Reset before blocking display operation
        display.showFeedCountdown(MANUAL_FEED_COUNTDOWN_SEC);
        esp_task_wdt_reset();  // Reset after display operation

        // Countdown with periodic display updates
        bool cancelled = false;
        for (int i = MANUAL_FEED_COUNTDOWN_SEC; i > 0 && !cancelled; i--) {
            DEBUG_PRINTF("Feed in %d...\n", i);
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
                    DEBUG_PRINTLN("Feed cancelled by button press");
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

            DEBUG_PRINTF("Feeding now! (%d seconds)\n", MANUAL_FEED_DURATION_SEC);
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
    DEBUG_PRINTLN("Entering deep sleep...");

    // Calculate next wake time
    uint64_t sleepTimeUs = calculateNextWakeTime();

    if (sleepTimeUs > 0) {
        DEBUG_PRINTF("Will wake in %llu seconds\n", sleepTimeUs / 1000000ULL);
        esp_sleep_enable_timer_wakeup(sleepTimeUs);
    } else {
        DEBUG_PRINTLN("No scheduled wake time, will wake on button only");
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

// Get the effective feed time for a schedule (handles sunrise/sunset calculation)
// Returns LOCAL time for comparison with local current time
// Returns true if valid time was calculated, false if location not set for sun-based schedules
bool getScheduleEffectiveTime(const Schedule* sched, const DateTime& localNow, int16_t tzOffset, int& hour, int& minute) {
    Settings& settings = storage.getSettings();

    if (sched->scheduleType == ScheduleType::SPECIFIC_TIME) {
        // Schedules are stored in local time
        hour = sched->hour;
        minute = sched->minute;
        return true;
    }

    // For sunrise/sunset, we need location
    if (!settings.locationSet) {
        return false;  // Can't calculate without location
    }

    // SunCalc returns local time (minutes from midnight)
    int sunMinutes;
    if (sched->scheduleType == ScheduleType::SUNRISE) {
        sunMinutes = SunCalc::getSunrise(localNow.year(), localNow.month(), localNow.day(),
                                          settings.latitude, settings.longitude, tzOffset);
    } else {  // SUNSET
        sunMinutes = SunCalc::getSunset(localNow.year(), localNow.month(), localNow.day(),
                                         settings.latitude, settings.longitude, tzOffset);
    }

    if (sunMinutes < 0) {
        return false;  // Sun doesn't rise/set at this location on this date
    }

    // Apply offset (sunOffset is in minutes, can be negative)
    sunMinutes += sched->sunOffset;

    // Normalize to valid range
    while (sunMinutes < 0) sunMinutes += 1440;
    while (sunMinutes >= 1440) sunMinutes -= 1440;

    // Return local time (no UTC conversion needed since we compare in local time)
    hour = sunMinutes / 60;
    minute = sunMinutes % 60;
    return true;
}

void checkFeedSchedules() {
    if (storage.getSettings().vacationMode) {
        return;  // Vacation mode - no feeding
    }

    // Only check schedules if time is synced
    if (!rtcManager.isTimeSynced()) {
        return;
    }

    // Get current UTC time from RTC
    DateTime now = rtcManager.now();
    time_t utcTime = now.unixtime();

    // Convert to local time using system timezone (handles DST automatically)
    // This works because we called setenv("TZ", ...) and tzset() in applyTimezone()
    struct tm localTm;
    localtime_r(&utcTime, &localTm);

    int currentHour = localTm.tm_hour;
    int currentMinute = localTm.tm_min;
    int currentDay = localTm.tm_wday;  // 0 = Sunday
    int currentMonth = localTm.tm_mon + 1;
    int currentDayOfMonth = localTm.tm_mday;

    // For sunrise/sunset calculations, create a local DateTime
    // (mktime normalizes the struct tm to a timestamp, then we create DateTime from it)
    time_t localTime = mktime(&localTm);
    DateTime localNow = DateTime((uint32_t)localTime);
    int16_t tzOffset = storage.getSettings().timezoneOffset;  // Still needed for SunCalc

    // Reset executed mask if it's a new local day
    if (currentDayOfMonth != lastExecutedScheduleDay) {
        lastExecutedScheduleDay = currentDayOfMonth;
        executedScheduleMask = 0;
    }

    int scheduleCount = storage.getScheduleCount();
    for (int i = 0; i < scheduleCount; i++) {
        Schedule* sched = storage.getSchedule(i);
        if (!sched || !sched->enabled) continue;

        // Check if already executed today (use array index, not ID which can exceed 31)
        if (executedScheduleMask & (1U << i)) continue;

        // Check day of week
        if (!sched->isActiveOnDay(currentDay)) continue;

        // Check seasonal dates
        if (!sched->isActiveOnDate(currentMonth, currentDayOfMonth)) continue;

        // Get effective time (handles sunrise/sunset calculation)
        int schedHour, schedMinute;
        if (!getScheduleEffectiveTime(sched, localNow, tzOffset, schedHour, schedMinute)) {
            continue;  // Skip if can't calculate time (e.g., location not set)
        }

        // Check if it's time to run this schedule
        // Convert both times to minutes-since-midnight for easier comparison
        int currentTotalMinutes = currentHour * 60 + currentMinute;
        int schedTotalMinutes = schedHour * 60 + schedMinute;

        // Match if we're at exactly the scheduled minute
        // (checking every 10 seconds ensures we catch the right minute)
        if (currentTotalMinutes == schedTotalMinutes) {
            DEBUG_PRINTF("Schedule %s triggered at %02d:%02d (current: %02d:%02d)\n",
                        sched->name, schedHour, schedMinute, currentHour, currentMinute);

            // Mark as executed (even if we skip due to overlap/running)
            // Use array index, not ID (ID can exceed 31 causing undefined behavior)
            executedScheduleMask |= (1U << i);

            // Check if motor is already running (overlap protection)
            if (motor.isRunning()) {
                DEBUG_PRINTLN("Motor already running - skipping feed");
                storage.logFeedEvent(0, false, sched->name, FeedStatus::SKIPPED_RUNNING);
                continue;
            }

            // Check if a feed was recently completed (overlap protection)
            uint32_t timeSinceLastFeed = millis() - lastFeedCompletedTime;
            if (lastFeedCompletedTime > 0 && timeSinceLastFeed < MIN_FEED_INTERVAL_MS) {
                DEBUG_PRINTF("Feed skipped - only %lu ms since last feed\n", timeSinceLastFeed);
                storage.logFeedEvent(0, false, sched->name, FeedStatus::SKIPPED_RECENT);
                continue;
            }

            // Check battery before running motor
            if (battery.getStatus() == BatteryStatus::CRITICAL) {
                DEBUG_PRINTLN("Battery critical - skipping feed");
                storage.logFeedEvent(0, false, sched->name, FeedStatus::SKIPPED_BATTERY);
                continue;
            }

            DEBUG_PRINTF("Executing feed schedule: %s\n", sched->name);

            // Run motor with schedule-specific or default duration
            uint8_t duration = sched->duration > 0 ? sched->duration : motor.getDefaultDuration();
            motor.startThrow(duration);

            // Log feed event to history
            storage.logFeedEvent(duration, false, sched->name, FeedStatus::EXECUTED);

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

    Settings& settings = storage.getSettings();
    int16_t tzOffset = settings.timezoneOffset;

    // Get current local time using system timezone (same approach as getNextRunTime)
    time_t utcNow = time(nullptr);
    struct tm localTm;
    localtime_r(&utcNow, &localTm);

    int currentDay = localTm.tm_wday;
    int currentHour = localTm.tm_hour;
    int currentMinute = localTm.tm_min;
    int currentMonth = localTm.tm_mon + 1;
    int currentDayOfMonth = localTm.tm_mday;
    uint32_t currentDaySeconds = currentHour * 3600 + currentMinute * 60;

    // Create DateTime for sunrise/sunset calculations
    time_t localTime = mktime(&localTm);
    DateTime localNow = DateTime((uint32_t)localTime);

    uint32_t nearestSeconds = UINT32_MAX;
    int nearestDayOffset = -1;
    int nearestHour = 0;
    int nearestMinute = 0;

    int scheduleCount = storage.getScheduleCount();

    for (int dayOffset = 0; dayOffset < 7; dayOffset++) {
        int checkDay = (currentDay + dayOffset) % 7;
        uint32_t baseSeconds = dayOffset * 86400;

        // Calculate the date for this day offset (needed for sunrise/sunset and date filtering)
        DateTime checkDate = DateTime(localNow.unixtime() + (dayOffset * 86400));

        for (int i = 0; i < scheduleCount; i++) {
            Schedule* sched = storage.getSchedule(i);
            if (!sched || !sched->enabled) continue;
            if (!sched->isActiveOnDay(checkDay)) continue;
            if (!sched->isActiveOnDate(checkDate.month(), checkDate.day())) continue;

            // Calculate the effective time for this schedule
            int schedHour, schedMinute;

            if (sched->scheduleType == ScheduleType::SPECIFIC_TIME) {
                schedHour = sched->hour;
                schedMinute = sched->minute;
            } else if (settings.locationSet) {
                // Calculate sunrise/sunset for the target day
                int sunMinutes;
                if (sched->scheduleType == ScheduleType::SUNRISE) {
                    sunMinutes = SunCalc::getSunrise(checkDate.year(), checkDate.month(), checkDate.day(),
                                                      settings.latitude, settings.longitude, tzOffset);
                } else {
                    sunMinutes = SunCalc::getSunset(checkDate.year(), checkDate.month(), checkDate.day(),
                                                     settings.latitude, settings.longitude, tzOffset);
                }

                if (sunMinutes < 0) continue;

                sunMinutes += sched->sunOffset;
                while (sunMinutes < 0) sunMinutes += 1440;
                while (sunMinutes >= 1440) sunMinutes -= 1440;

                schedHour = sunMinutes / 60;
                schedMinute = sunMinutes % 60;
            } else {
                continue;  // Can't calculate without location
            }

            uint32_t schedSeconds = schedHour * 3600 + schedMinute * 60;

            // Skip if this is today and the time has passed
            if (dayOffset == 0 && schedSeconds <= currentDaySeconds) {
                continue;
            }

            uint32_t totalSeconds = baseSeconds + schedSeconds - currentDaySeconds;
            if (totalSeconds < nearestSeconds) {
                nearestSeconds = totalSeconds;
                nearestDayOffset = dayOffset;
                nearestHour = schedHour;
                nearestMinute = schedMinute;
            }
        }
    }

    // Only format if we actually found a schedule
    if (nearestDayOffset < 0) {
        return;  // Keep "None" default
    }

    // Use centralized 12-hour formatting (already in local time)
    FormattedTime ft(nearestHour);

    // Format as "Today H:MM AM" or "Mon H:MM PM"
    if (nearestDayOffset == 0) {
        snprintf(buffer, len, "Today %d:%02d %s", ft.displayHour, nearestMinute, ft.ampm);
    } else {
        int displayDay = (currentDay + nearestDayOffset) % 7;
        snprintf(buffer, len, "%s %d:%02d %s", DAY_NAMES[displayDay], ft.displayHour, nearestMinute, ft.ampm);
    }
}
