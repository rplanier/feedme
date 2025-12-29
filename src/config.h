#pragma once

#include <Arduino.h>

// =============================================================================
// Version
// =============================================================================

constexpr char FEEDME_VERSION[] = "1.0";

// =============================================================================
// Pin Definitions (T-Display-S3)
// =============================================================================

// Buttons (directly on T-Display-S3 board)
// Swapped for right-handed use in landscape orientation
constexpr uint8_t PIN_BUTTON_TOP = 14;     // GPIO14, top in landscape (right-handed)
constexpr uint8_t PIN_BUTTON_BOTTOM = 0;   // GPIO0 (Boot button), bottom in landscape

// Motor control
constexpr uint8_t PIN_MOTOR_RELAY = 21;    // External relay/MOSFET

// Battery sensing (external voltage divider required for 12V)
constexpr uint8_t PIN_BATTERY_ADC = 3;     // ADC capable GPIO (GPIO3 on T-Display-S3)

// Display backlight (T-Display-S3 uses GPIO 38)
constexpr uint8_t PIN_TFT_BACKLIGHT = 38;

// =============================================================================
// Display Settings
// =============================================================================

// Rotation: 1 or 3 for landscape (buttons on right side)
constexpr uint8_t DISPLAY_ROTATION = 1;

// Screen dimensions after rotation
constexpr uint16_t SCREEN_WIDTH = 320;
constexpr uint16_t SCREEN_HEIGHT = 170;

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

// Inverted motor control (true for 2N7000 + MOSFET gate driver circuit)
// When true: GPIO LOW = motor ON, GPIO HIGH = motor OFF
constexpr bool MOTOR_INVERTED = true;

// =============================================================================
// Battery Settings
// =============================================================================

// Battery types with different discharge characteristics
enum class BatteryType : uint8_t {
    SLA = 0,    // Standard Sealed Lead Acid / Flooded
    AGM = 1,    // Absorbed Glass Mat (deeper discharge tolerant)
    GEL = 2     // Gel cell (deeper discharge tolerant)
};

// Voltage divider ratio: if using 100K/22K, ratio = (100+22)/22 = 5.545
// Adjust based on actual resistor values
constexpr float BATTERY_DIVIDER_RATIO = 5.545f;

// ADC reference voltage
constexpr float ADC_REFERENCE_VOLTAGE = 3.3f;

// ADC resolution (ESP32-S3 is 12-bit)
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
constexpr float BATTERY_VOLTAGE_GOOD = 12.5f;   // ~75%+ charge
constexpr float BATTERY_VOLTAGE_OKAY = 12.2f;   // ~50%+ charge
constexpr float BATTERY_VOLTAGE_LOW = 11.9f;    // ~25%+ charge

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
// Storage Keys (Preferences namespace)
// =============================================================================

constexpr char PREF_NAMESPACE[] = "feedme";
constexpr char PREF_DEVICE_ID[] = "deviceId";
constexpr char PREF_MOTOR_DURATION[] = "motorDur";
constexpr char PREF_VACATION_MODE[] = "vacation";

// LittleFS paths
constexpr char SCHEDULES_FILE[] = "/schedules.json";
