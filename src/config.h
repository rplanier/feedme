#pragma once

#include <Arduino.h>

// =============================================================================
// Version
// =============================================================================

constexpr char FEEDME_VERSION[] = "0.1b";

// =============================================================================
// Serial Debug Output
// =============================================================================

// IMPORTANT: Serial debug must be disabled because GPIO16 (D6) is used for the
// button input, but GPIO16 is also U0TXD. Serial output conflicts with the button.
// Set to 0 to disable, 1 to enable (only for debugging with different pin config)
#define SERIAL_DEBUG 1

#if SERIAL_DEBUG
    #define DEBUG_PRINT(...) Serial.print(__VA_ARGS__)
    #define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
    #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
    #define DEBUG_PRINT(...) ((void)0)
    #define DEBUG_PRINTLN(...) ((void)0)
    #define DEBUG_PRINTF(...) ((void)0)
#endif

// =============================================================================
// Hardware Target
// =============================================================================

// Define which hardware target we're building for
// Uncomment ONE of these:
// #define TARGET_XIAO_ESP32C3    // Test board
#define TARGET_XIAO_ESP32C6       // Target production board (better BLE range)
// #define TARGET_ESP32C3_SUPERMINI  // Original board (has antenna issues)

// =============================================================================
// Antenna Type (defined early for use in pin definitions)
// =============================================================================

// Antenna type for BLE/WiFi communication
enum class AntennaType : uint8_t {
    ROD = 0,      // External rod antenna (default, better range)
    ONBOARD = 1   // Onboard PCB antenna (compact, reduced range)
};

// =============================================================================
// Pin Definitions
// =============================================================================

#if defined(TARGET_XIAO_ESP32C3)
// -----------------------------------------------------------------------------
// Seeed XIAO ESP32-C3 + 2.9" E-Paper
// Available GPIO: 2, 3, 4, 5, 6, 7, 8, 9, 10, 20, 21 (11 pins)
// Single-button UI: press = next page, hold = context action (e.g., toggle WiFi)
// Using internal RTC (no DS3231) - frees GPIO 8/9
// D0 (GPIO 2) reserved for future solar ADC
// -----------------------------------------------------------------------------

// E-Paper Display (SPI)
constexpr uint8_t PIN_EPD_MOSI = 6;   // D4
constexpr uint8_t PIN_EPD_CLK = 4;    // D2
constexpr uint8_t PIN_EPD_CS = 7;     // D5
constexpr uint8_t PIN_EPD_DC = 5;     // D3
constexpr uint8_t PIN_EPD_RST = 10;   // D10
constexpr uint8_t PIN_EPD_BUSY = 20;  // D7

// DS3231 RTC (I2C) - not used, using internal RTC instead
// Pins reserved but available for other uses
constexpr uint8_t PIN_RTC_SDA = 8;    // D8 - free
constexpr uint8_t PIN_RTC_SCL = 9;    // D9 - used for BOOT button

// Single button mode: using onboard BOOT button (GPIO 9)
// Press = next page, hold = context action (toggle WiFi)
#define SINGLE_BUTTON_MODE 1
constexpr uint8_t PIN_BUTTON = 9;         // BOOT button (onboard)
constexpr uint8_t PIN_BUTTON_PREV = 9;    // Alias for compatibility
constexpr uint8_t PIN_BUTTON_NEXT = 9;    // Alias for compatibility

// Motor control
constexpr uint8_t PIN_MOTOR_RELAY = 21;   // D6

// Battery sensing
constexpr uint8_t PIN_BATTERY_ADC = 3;    // D1/A1 (ADC capable)

// Future: Solar panel voltage sensing
// constexpr uint8_t PIN_SOLAR_ADC = 2;   // D0/A0 (ADC capable)

#elif defined(TARGET_XIAO_ESP32C6)
// -----------------------------------------------------------------------------
// Seeed XIAO ESP32-C6 + 2.9" E-Paper (production target - better BLE range)
// Pin assignments match FeedMe PCB v0.2
// D-pin to GPIO mapping for C6 (from Seeed wiki):
//   D0=GPIO0, D1=GPIO1, D2=GPIO2, D3=GPIO21, D4=GPIO22, D5=GPIO23,
//   D6=GPIO16, D7=GPIO17, D8=GPIO19, D9=GPIO20, D10=GPIO18
// -----------------------------------------------------------------------------

// E-Paper Display (SPI)
constexpr uint8_t PIN_EPD_CLK = 2;     // D2 - SPI Clock
constexpr uint8_t PIN_EPD_DC = 21;     // D3 - Data/Command
constexpr uint8_t PIN_EPD_MOSI = 22;   // D4 - SPI MOSI (DIN)
constexpr uint8_t PIN_EPD_CS = 23;     // D5 - Chip Select
constexpr int8_t  PIN_EPD_RST = -1;    // RST has external pull-up on PCB (directly to 3V3)
constexpr int8_t  PIN_EPD_BUSY = 18;   // D10 (GPIO18) - EPD BUSY pin

// DS3231M RTC (I2C)
constexpr uint8_t PIN_RTC_SDA = 17;    // D7 - I2C Data
constexpr uint8_t PIN_RTC_SCL = 19;    // D8 - I2C Clock

// Buttons
// BTN1 on D6, BTN2 on D10 (active when solder jumper JP1 in BTN2 position)
// When JP1 in EPD_BUSY position, single-button mode using BTN1 only
#define SINGLE_BUTTON_MODE 1              // Set to 0 if JP1 connects D10 to BTN2
constexpr uint8_t PIN_BUTTON = 16;         // D6 - BTN1 (directly wired to button)
constexpr uint8_t PIN_BUTTON_PREV = 16;    // D6 - BTN1
constexpr uint8_t PIN_BUTTON_NEXT = 18;    // D10 - BTN2 (shared with EPD_BUSY via JP1)

// Motor control (P-channel high-side via 2N7002 gate driver)
constexpr uint8_t PIN_MOTOR_RELAY = 20;   // D9 - MOTOR_CTRL

// Battery sensing (voltage divider 100K/27K)
constexpr uint8_t PIN_BATTERY_ADC = 1;    // D1 - BATT_SENSE (ADC capable)

// Solar panel voltage sensing (voltage divider 100K/27K)
constexpr uint8_t PIN_SOLAR_ADC = 0;      // D0 - SOLAR_SENSE (ADC capable)

// RF switch for external antenna (FM8625H)
constexpr uint8_t PIN_RF_SW_PWR = 3;      // RF switch power pin
constexpr uint8_t PIN_RF_PORT = 14;       // RF port select pin

// Default antenna type - onboard is safer default (rod antenna requires external connection)
constexpr AntennaType DEFAULT_ANTENNA_TYPE = AntennaType::ONBOARD;

#elif defined(TARGET_ESP32C3_SUPERMINI)
// -----------------------------------------------------------------------------
// ESP32-C3 SuperMini + 2.9" E-Paper (original board - has antenna issues)
// -----------------------------------------------------------------------------

// E-Paper Display (SPI)
constexpr uint8_t PIN_EPD_MOSI = 6;
constexpr uint8_t PIN_EPD_CLK = 4;
constexpr uint8_t PIN_EPD_CS = 7;
constexpr uint8_t PIN_EPD_DC = 5;
constexpr uint8_t PIN_EPD_RST = 3;
constexpr uint8_t PIN_EPD_BUSY = 2;

// DS3231 RTC (I2C)
constexpr uint8_t PIN_RTC_SDA = 8;
constexpr uint8_t PIN_RTC_SCL = 9;

// Dual button mode
#define SINGLE_BUTTON_MODE 0
constexpr uint8_t PIN_BUTTON_PREV = 10;   // Previous page / scroll up
constexpr uint8_t PIN_BUTTON_NEXT = 1;    // Next page / scroll down

// Motor control
constexpr uint8_t PIN_MOTOR_RELAY = 21;   // External relay/MOSFET

// Battery sensing (external voltage divider required for 12V)
constexpr uint8_t PIN_BATTERY_ADC = 0;    // ADC1_CH0 on ESP32-C3

#else
#error "No hardware target defined! Define TARGET_XIAO_ESP32C3, TARGET_XIAO_ESP32C6, or TARGET_ESP32C3_SUPERMINI"
#endif

// =============================================================================
// Display Settings (2.9" E-Paper 296×128)
// =============================================================================

constexpr uint16_t SCREEN_WIDTH = 296;
constexpr uint16_t SCREEN_HEIGHT = 128;

// E-paper refresh settings
constexpr uint8_t PARTIAL_REFRESH_LIMIT = 10;  // Full refresh after this many partials

// =============================================================================
// Button Timing
// =============================================================================

constexpr uint32_t BUTTON_DEBOUNCE_MS = 50;
constexpr uint32_t BUTTON_HOLD_THRESHOLD_MS = 500;

// =============================================================================
// Motor Settings
// =============================================================================

// Default motor run time in seconds
constexpr uint8_t MOTOR_DEFAULT_DURATION_SEC = 5;

// Absolute maximum motor run time (safety cutoff)
constexpr uint8_t MOTOR_MAX_DURATION_SEC = 30;

// Inverted motor control
// For N-channel low-side + 2N7000: true (GPIO LOW = motor ON)
// For P-channel high-side + 2N7000: false (GPIO HIGH = motor ON)
constexpr bool MOTOR_INVERTED = false;

// =============================================================================
// Battery Settings
// =============================================================================

// Sleep/display timeout options (for e-paper display power saving)
enum class SleepTimeout : uint8_t {
    TIMEOUT_15S = 15,
    TIMEOUT_30S = 30,
    TIMEOUT_1M = 60,
    TIMEOUT_2M = 120,
    TIMEOUT_5M = 255,   // Special value for 5 min (300 doesn't fit in uint8_t)
    TIMEOUT_NEVER = 0
};

constexpr SleepTimeout DEFAULT_SLEEP_TIMEOUT = SleepTimeout::TIMEOUT_30S;

// Helper function to get actual seconds from SleepTimeout enum
inline uint16_t getSleepTimeoutSeconds(SleepTimeout timeout) {
    switch (timeout) {
        case SleepTimeout::TIMEOUT_15S: return 15;
        case SleepTimeout::TIMEOUT_30S: return 30;
        case SleepTimeout::TIMEOUT_1M: return 60;
        case SleepTimeout::TIMEOUT_2M: return 120;
        case SleepTimeout::TIMEOUT_5M: return 300;
        case SleepTimeout::TIMEOUT_NEVER: return 0;
        default: return 30;
    }
}

// =============================================================================
// Inactivity Timeout Settings (for BLE light sleep power optimization)
// =============================================================================
// After this timeout with no BLE activity, device enters light sleep with BLE
// advertising (low power) or deep sleep (outside BLE windows)

constexpr uint8_t INACTIVITY_MIN_MINUTES = 3;      // Minimum timeout (minutes)
constexpr uint8_t INACTIVITY_MAX_MINUTES = 60;     // Maximum timeout (minutes)
constexpr uint8_t DEFAULT_INACTIVITY_TIMEOUT_MIN = 5;  // Default: 5 minutes
constexpr uint8_t INACTIVITY_NEVER = 0;            // 0 = never enter light sleep

// Voltage divider ratio: theoretical 100K/22K = 5.545
// Calibrated based on actual measurements (accounts for resistor tolerance and ADC error)
constexpr float BATTERY_DIVIDER_RATIO = 5.95f;

// ADC reference voltage
constexpr float ADC_REFERENCE_VOLTAGE = 3.3f;

// ADC resolution (ESP32-C3 is 12-bit)
constexpr uint16_t ADC_MAX_VALUE = 4095;

// Battery chemistry type (affects state-of-charge thresholds)
enum class BatteryType : uint8_t {
    SLA = 0,    // Sealed Lead Acid (standard)
    AGM = 1,    // Absorbent Glass Mat (slightly higher voltages)
    GEL = 2     // Gel Cell (slightly lower voltages)
};

// Schedule trigger type (specific time vs sunrise/sunset)
enum class ScheduleType : uint8_t {
    SPECIFIC_TIME = 0,  // Feed at exact hour:minute
    SUNRISE = 1,        // Feed at sunrise +/- offset
    SUNSET = 2          // Feed at sunset +/- offset
};

// Battery status thresholds (lead-acid, based on state of charge curves)
// These are open-circuit voltages - under load will be slightly lower
//
// State of Charge vs Voltage (12V battery, 6 cells / 6V battery, 3 cells):
//   100% = 12.7V / 6.35V (2.12V/cell)
//    75% = 12.4V / 6.20V (2.07V/cell)
//    50% = 12.2V / 6.10V (2.03V/cell)
//    25% = 12.0V / 6.00V (2.00V/cell)
//     0% = 11.8V / 5.90V (1.97V/cell)
//  Damage risk below 10.5V / 5.25V (1.75V/cell)
//
// Auto-detection: voltage > 9V = 12V battery, <= 9V = 6V battery
// Chemistry (SLA/AGM/GEL) is user-configurable and affects Good/Fair/Low thresholds

// 12V SLA (Sealed Lead Acid) thresholds - standard lead-acid
constexpr float BATTERY_12V_SLA_GOOD = 12.4f;      // ~75%+ charge
constexpr float BATTERY_12V_SLA_FAIR = 12.0f;      // ~25%+ charge
constexpr float BATTERY_12V_SLA_LOW = 11.5f;       // Depleted, charge soon

// 12V AGM (Absorbent Glass Mat) thresholds - slightly higher resting voltage
constexpr float BATTERY_12V_AGM_GOOD = 12.5f;      // ~75%+ charge
constexpr float BATTERY_12V_AGM_FAIR = 12.1f;      // ~25%+ charge
constexpr float BATTERY_12V_AGM_LOW = 11.6f;       // Depleted, charge soon

// 12V GEL thresholds - similar to SLA, slightly more sensitive
constexpr float BATTERY_12V_GEL_GOOD = 12.4f;      // ~75%+ charge
constexpr float BATTERY_12V_GEL_FAIR = 11.9f;      // ~25%+ charge
constexpr float BATTERY_12V_GEL_LOW = 11.4f;       // Depleted, charge soon

// 12V critical threshold - same for all chemistries (damage risk)
constexpr float BATTERY_12V_CRITICAL = 10.8f;      // Risk of damage, disable feeds

// 6V SLA thresholds
constexpr float BATTERY_6V_SLA_GOOD = 6.2f;        // ~75%+ charge
constexpr float BATTERY_6V_SLA_FAIR = 6.0f;        // ~25%+ charge
constexpr float BATTERY_6V_SLA_LOW = 5.75f;        // Depleted, charge soon

// 6V AGM thresholds
constexpr float BATTERY_6V_AGM_GOOD = 6.25f;       // ~75%+ charge
constexpr float BATTERY_6V_AGM_FAIR = 6.05f;       // ~25%+ charge
constexpr float BATTERY_6V_AGM_LOW = 5.8f;         // Depleted, charge soon

// 6V GEL thresholds
constexpr float BATTERY_6V_GEL_GOOD = 6.2f;        // ~75%+ charge
constexpr float BATTERY_6V_GEL_FAIR = 5.95f;       // ~25%+ charge
constexpr float BATTERY_6V_GEL_LOW = 5.7f;         // Depleted, charge soon

// 6V critical threshold - same for all chemistries (damage risk)
constexpr float BATTERY_6V_CRITICAL = 5.4f;        // Risk of damage, disable feeds

// Threshold for auto-detecting 6V vs 12V battery
constexpr float BATTERY_TYPE_THRESHOLD = 9.0f;     // > 9V = 12V battery, <= 9V = 6V battery

// Solar panel charging detection threshold
// If solar panel voltage exceeds this, charging is detected
// A 12V panel will produce ~17V open circuit, ~14V under load
constexpr float SOLAR_CHARGING_THRESHOLD = 5.0f;

// =============================================================================
// WiFi Settings
// =============================================================================

// SSID and password format (XXXX replaced with device ID)
constexpr char WIFI_SSID_PREFIX[] = "FeedMe-";
constexpr char WIFI_PASS_PREFIX[] = "feedme-";

// WiFi timeout after last activity (milliseconds)
constexpr uint32_t WIFI_IDLE_TIMEOUT_MS = 5 * 60 * 1000;  // 5 minutes

// =============================================================================
// BLE Settings
// =============================================================================

// BLE service UUID
constexpr char BLE_SERVICE_UUID[] = "f33d0001-1234-5678-9abc-def012345678";

// =============================================================================
// Storage Keys (Preferences namespace)
// =============================================================================

constexpr char PREF_NAMESPACE[] = "feedme";
constexpr char PREF_DEVICE_ID[] = "deviceId";
constexpr char PREF_MOTOR_DURATION[] = "motorDur";
constexpr char PREF_VACATION_MODE[] = "vacation";
constexpr char PREF_TIME_SYNCED[] = "timeSynced";
constexpr char PREF_BATTERY_TYPE[] = "battType";
constexpr char PREF_LATITUDE[] = "latitude";
constexpr char PREF_LONGITUDE[] = "longitude";
constexpr char PREF_LOCATION_SET[] = "locSet";
constexpr char PREF_ANTENNA_TYPE[] = "antennaType";

// Paired device keys (for BLE pairing security)
constexpr char PREF_PAIRED_COUNT[] = "pairCount";     // Number of paired devices
constexpr char PREF_PAIRED_PREFIX[] = "paired";       // Prefix for paired device addresses (paired0, paired1, etc.)
constexpr char PREF_PIN_FAILS[] = "pinFails";         // Failed pairing attempt counter
constexpr char PREF_PIN_LOCKOUT[] = "pinLockout";     // Lockout end timestamp (millis)

// Legacy PIN keys (deprecated - kept for migration)
constexpr char PREF_PIN_HASH[] = "pinHash";           // SHA-256 hash of PIN + deviceId salt
constexpr char PREF_PIN_SET[] = "pinSet";             // Boolean: is PIN configured

// LittleFS paths
constexpr char SCHEDULES_FILE[] = "/schedules.json";
constexpr char BLE_SCHEDULES_FILE[] = "/ble_schedules.json";
constexpr char FEED_HISTORY_FILE[] = "/feed_history.json";

// =============================================================================
// Time/Day String Constants
// =============================================================================

// Day name abbreviations (short form for compact display)
inline const char* const DAY_ABBREV[] = {"Su", "M", "Tu", "W", "Th", "F", "Sa"};

// Day names (3-letter form)
inline const char* const DAY_NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// Month names (3-letter form)
inline const char* const MONTH_NAMES[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

// =============================================================================
// Day Bitmask Constants
// =============================================================================

constexpr uint8_t DAYS_ALL = 0x7F;        // All days (Sun-Sat, bits 0-6)
constexpr uint8_t DAYS_WEEKDAYS = 0x3E;   // Mon-Fri (bits 1-5)
constexpr uint8_t DAYS_WEEKENDS = 0x41;   // Sat-Sun (bits 0,6)

// =============================================================================
// Watchdog Timer
// =============================================================================

// Reset device if main loop hangs for this many seconds
constexpr uint32_t WATCHDOG_TIMEOUT_SEC = 30;

// =============================================================================
// Manual Feed from Display
// =============================================================================

// Countdown before manual feed starts (seconds) - gives time to cancel
constexpr uint8_t MANUAL_FEED_COUNTDOWN_SEC = 15;

// Fixed motor duration for manual feed from display (seconds)
constexpr uint8_t MANUAL_FEED_DURATION_SEC = 5;

// =============================================================================
// Debug/Diagnostics (defined in main.cpp)
// =============================================================================

// Get human-readable string for last reset reason
extern const char* getResetReasonString();
