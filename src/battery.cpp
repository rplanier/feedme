#include "battery.h"
#include "storage.h"

Battery battery;

void Battery::begin() {
    // Configure ADC pins
    pinMode(PIN_BATTERY_ADC, INPUT);
    pinMode(PIN_SOLAR_ADC, INPUT);

    // Set ADC resolution to 12 bits
    analogReadResolution(12);

    // Take initial samples to fill the buffer
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        samples[i] = readRawVoltage();
        delay(10);
    }
    samplesReady = true;
    sampleIndex = 0;

    // Calculate initial voltage
    float sum = 0;
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        sum += samples[i];
    }
    voltage = sum / SAMPLE_COUNT;
    previousVoltage = voltage;

    updateStatus();
}

void Battery::update() {
    uint32_t now = millis();
    if (now - lastReadTime < READ_INTERVAL_MS) {
        return;
    }
    lastReadTime = now;

    // Store previous voltage for charging detection
    previousVoltage = voltage;

    // Take new sample
    samples[sampleIndex] = readRawVoltage();
    sampleIndex = (sampleIndex + 1) % SAMPLE_COUNT;

    // Calculate smoothed voltage with outlier rejection
    // First, find median by sorting a copy
    float sorted[SAMPLE_COUNT];
    memcpy(sorted, samples, sizeof(samples));
    for (int i = 0; i < SAMPLE_COUNT - 1; i++) {
        for (int j = i + 1; j < SAMPLE_COUNT; j++) {
            if (sorted[j] < sorted[i]) {
                float temp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = temp;
            }
        }
    }
    float median = sorted[SAMPLE_COUNT / 2];

    // Average samples within 0.5V of median (reject outliers)
    float sum = 0;
    int validCount = 0;
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        if (abs(samples[i] - median) < 0.5f) {
            sum += samples[i];
            validCount++;
        }
    }

    if (validCount > 0) {
        voltage = sum / validCount;
    } else {
        voltage = median;  // Fallback to median if all are outliers
    }

    updateStatus();
}

float Battery::readRawVoltage() {
    // Read ADC value (take multiple reads and average for noise reduction)
    int rawSum = 0;
    for (int i = 0; i < 4; i++) {
        rawSum += analogRead(PIN_BATTERY_ADC);
        delayMicroseconds(100);
    }
    int rawValue = rawSum / 4;

    // Convert to voltage at ADC pin
    float adcVoltage = (rawValue / (float)ADC_MAX_VALUE) * ADC_REFERENCE_VOLTAGE;

    // Apply voltage divider ratio to get actual battery voltage
    float batteryVoltage = adcVoltage * BATTERY_DIVIDER_RATIO;

    // Debug output every 10 seconds (controlled by caller)
    static uint32_t lastDebugPrint = 0;
    if (millis() - lastDebugPrint >= 10000) {
        lastDebugPrint = millis();
        Serial.printf("Battery: raw=%d, adcV=%.3f, battV=%.2f\n",
                      rawValue, adcVoltage, batteryVoltage);
    }

    return batteryVoltage;
}

float Battery::readSolarVoltage() {
    // Read ADC value (take multiple reads and average for noise reduction)
    int rawSum = 0;
    for (int i = 0; i < 4; i++) {
        rawSum += analogRead(PIN_SOLAR_ADC);
        delayMicroseconds(100);
    }
    int rawValue = rawSum / 4;

    // Convert to voltage at ADC pin
    float adcVoltage = (rawValue / (float)ADC_MAX_VALUE) * ADC_REFERENCE_VOLTAGE;

    // Apply voltage divider ratio to get actual solar panel voltage
    // Using same divider ratio as battery (100K/27K)
    float panelVoltage = adcVoltage * BATTERY_DIVIDER_RATIO;

    return panelVoltage;
}

void Battery::updateStatus() {
    // Auto-detect 6V vs 12V battery based on voltage
    // Chemistry (SLA/AGM/GEL) comes from user settings
    float thresholdGood, thresholdFair, thresholdLow, thresholdCritical;
    bool is12V = (voltage > BATTERY_TYPE_THRESHOLD);
    BatteryType batteryType = storage.getSettings().batteryType;

    if (is12V) {
        // 12V battery - select thresholds based on chemistry
        thresholdCritical = BATTERY_12V_CRITICAL;  // Same for all chemistries
        switch (batteryType) {
            case BatteryType::AGM:
                thresholdGood = BATTERY_12V_AGM_GOOD;
                thresholdFair = BATTERY_12V_AGM_FAIR;
                thresholdLow = BATTERY_12V_AGM_LOW;
                break;
            case BatteryType::GEL:
                thresholdGood = BATTERY_12V_GEL_GOOD;
                thresholdFair = BATTERY_12V_GEL_FAIR;
                thresholdLow = BATTERY_12V_GEL_LOW;
                break;
            case BatteryType::SLA:
            default:
                thresholdGood = BATTERY_12V_SLA_GOOD;
                thresholdFair = BATTERY_12V_SLA_FAIR;
                thresholdLow = BATTERY_12V_SLA_LOW;
                break;
        }
    } else {
        // 6V battery - select thresholds based on chemistry
        thresholdCritical = BATTERY_6V_CRITICAL;  // Same for all chemistries
        switch (batteryType) {
            case BatteryType::AGM:
                thresholdGood = BATTERY_6V_AGM_GOOD;
                thresholdFair = BATTERY_6V_AGM_FAIR;
                thresholdLow = BATTERY_6V_AGM_LOW;
                break;
            case BatteryType::GEL:
                thresholdGood = BATTERY_6V_GEL_GOOD;
                thresholdFair = BATTERY_6V_GEL_FAIR;
                thresholdLow = BATTERY_6V_GEL_LOW;
                break;
            case BatteryType::SLA:
            default:
                thresholdGood = BATTERY_6V_SLA_GOOD;
                thresholdFair = BATTERY_6V_SLA_FAIR;
                thresholdLow = BATTERY_6V_SLA_LOW;
                break;
        }
    }

    // Determine battery status
    if (voltage >= thresholdGood) {
        status = BatteryStatus::GOOD;
    } else if (voltage >= thresholdFair) {
        status = BatteryStatus::FAIR;
    } else if (voltage >= thresholdLow) {
        status = BatteryStatus::POOR;
    } else if (voltage >= thresholdCritical) {
        status = BatteryStatus::POOR;  // Still low, but not critical yet
    } else {
        status = BatteryStatus::CRITICAL;
    }

    // Detect charging: read solar panel voltage directly
    // Charging is detected when solar panel is producing voltage
    solarVoltage = readSolarVoltage();
    charging = (solarVoltage >= SOLAR_CHARGING_THRESHOLD);
}

const char* Battery::getStatusText() const {
    switch (status) {
        case BatteryStatus::GOOD:
            return "Good";
        case BatteryStatus::FAIR:
            return "Fair";
        case BatteryStatus::POOR:
            return "Low";
        case BatteryStatus::CRITICAL:
            return "Critical";
        default:
            return "???";
    }
}

int Battery::getPercentage() const {
    // Linear mapping based on typical lead-acid battery voltage ranges
    if (is12V()) {
        // 12V battery: 10.8V (0%) to 12.7V (100%)
        return constrain(map(voltage * 10, 108, 127, 0, 100), 0, 100);
    } else {
        // 6V battery: 5.4V (0%) to 6.4V (100%)
        return constrain(map(voltage * 10, 54, 64, 0, 100), 0, 100);
    }
}
