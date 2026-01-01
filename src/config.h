#pragma once

#include <Arduino.h>

// =============================================================================
// Version
// =============================================================================

constexpr char FEEDME_VERSION[] = "0.1b";

// =============================================================================
// Hardware Target
// =============================================================================

// Define which hardware target we're building for
// Uncomment ONE of these:
#define TARGET_XIAO_ESP32C3    // Needs single-button UI (only 11 GPIO pins)
// #define TARGET_ESP32C3_SUPERMINI  // Original board (has antenna issues)

// =============================================================================
// Pin Definitions
// =============================================================================

#if defined(TARGET_XIAO_ESP32C3)
// -----------------------------------------------------------------------------
// Seeed XIAO ESP32-C3 + 2.9" E-Paper
// Available GPIO: 2, 3, 4, 5, 6, 7, 8, 9, 10, 20, 21 (11 pins)
// Single-button UI: press = next page, hold = context action (e.g., toggle WiFi)
// -----------------------------------------------------------------------------

// E-Paper Display (SPI)
constexpr uint8_t PIN_EPD_MOSI = 6;   // D4
constexpr uint8_t PIN_EPD_CLK = 4;    // D2
constexpr uint8_t PIN_EPD_CS = 7;     // D5
constexpr uint8_t PIN_EPD_DC = 5;     // D3
constexpr uint8_t PIN_EPD_RST = 10;   // D10
constexpr uint8_t PIN_EPD_BUSY = 20;  // D7

// DS3231 RTC (I2C)
constexpr uint8_t PIN_RTC_SDA = 8;    // D8
constexpr uint8_t PIN_RTC_SCL = 9;    // D9

// Single button mode: press = next page, hold = context action
constexpr bool SINGLE_BUTTON_MODE = true;
constexpr uint8_t PIN_BUTTON = 2;         // D0 - the only button
constexpr uint8_t PIN_BUTTON_PREV = 2;    // Alias for compatibility
constexpr uint8_t PIN_BUTTON_NEXT = 2;    // Alias for compatibility

// Motor control
constexpr uint8_t PIN_MOTOR_RELAY = 21;   // D6

// Battery sensing
constexpr uint8_t PIN_BATTERY_ADC = 3;    // D1/A1 (ADC capable)

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
constexpr bool SINGLE_BUTTON_MODE = false;
constexpr uint8_t PIN_BUTTON_PREV = 10;   // Previous page / scroll up
constexpr uint8_t PIN_BUTTON_NEXT = 1;    // Next page / scroll down

// Motor control
constexpr uint8_t PIN_MOTOR_RELAY = 21;   // External relay/MOSFET

// Battery sensing (external voltage divider required for 12V)
constexpr uint8_t PIN_BATTERY_ADC = 0;    // ADC1_CH0 on ESP32-C3

#else
#error "No hardware target defined! Define TARGET_XIAO_ESP32C3 or TARGET_ESP32C3_SUPERMINI"
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

// Battery types with different discharge characteristics
enum class BatteryType : uint8_t {
    SLA = 0,    // Standard Sealed Lead Acid / Flooded
    AGM = 1,    // Absorbed Glass Mat (deeper discharge tolerant)
    GEL = 2     // Gel cell (deeper discharge tolerant)
};

// Sleep/display timeout options
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

// Voltage divider ratio: theoretical 100K/22K = 5.545
// Calibrated based on actual measurements (accounts for resistor tolerance and ADC error)
constexpr float BATTERY_DIVIDER_RATIO = 5.95f;

// ADC reference voltage
constexpr float ADC_REFERENCE_VOLTAGE = 3.3f;

// ADC resolution (ESP32-C3 is 12-bit)
constexpr uint16_t ADC_MAX_VALUE = 4095;

// Battery status thresholds (12V lead-acid, based on state of charge curves)
// These are open-circuit voltages - under load will be slightly lower
//
// State of Charge vs Voltage (12V battery, 6 cells):
//   100% = 12.7V (2.12V/cell)
//    75% = 12.4V (2.07V/cell)
//    50% = 12.2V (2.03V/cell)
//    25% = 12.0V (2.00V/cell)
//     0% = 11.8V (1.97V/cell)
//
// Note: Thresholds slightly below round numbers to match display rounding
constexpr float BATTERY_VOLTAGE_GOOD = 12.45f;  // ~75%+ charge (displays as 12.5V+)
constexpr float BATTERY_VOLTAGE_OKAY = 12.15f;  // ~50%+ charge (displays as 12.2V+)
constexpr float BATTERY_VOLTAGE_LOW = 11.85f;   // ~25%+ charge (displays as 11.9V+)

// Critical thresholds vary by battery type (below this = disable motor)
// SLA/Flooded: more sensitive to deep discharge
// AGM/Gel: can tolerate deeper discharge without damage
constexpr float BATTERY_CRITICAL_SLA = 11.8f;   // Don't go below 0% SOC
constexpr float BATTERY_CRITICAL_AGM = 11.5f;   // Can go slightly deeper
constexpr float BATTERY_CRITICAL_GEL = 11.5f;   // Can go slightly deeper

// Charging detection: voltage above this suggests solar is charging
constexpr float BATTERY_CHARGING_THRESHOLD = 13.0f;

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

// BLE service and characteristic UUIDs
constexpr char BLE_SERVICE_UUID[] = "f33d0001-1234-5678-9abc-def012345678";
constexpr char BLE_WAKE_CHAR_UUID[] = "f33d0002-1234-5678-9abc-def012345678";

// =============================================================================
// Storage Keys (Preferences namespace)
// =============================================================================

constexpr char PREF_NAMESPACE[] = "feedme";
constexpr char PREF_DEVICE_ID[] = "deviceId";
constexpr char PREF_MOTOR_DURATION[] = "motorDur";
constexpr char PREF_VACATION_MODE[] = "vacation";
constexpr char PREF_TIME_SYNCED[] = "timeSynced";
constexpr char PREF_BATTERY_TYPE[] = "battType";

// LittleFS paths
constexpr char SCHEDULES_FILE[] = "/schedules.json";
constexpr char BLE_SCHEDULES_FILE[] = "/ble_schedules.json";
