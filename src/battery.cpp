#include "battery.h"
#include "storage.h"

Battery battery;

void Battery::begin() {
    // Configure ADC pin
    pinMode(PIN_BATTERY_ADC, INPUT);

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

void Battery::updateStatus() {
    // Get critical threshold based on battery type
    float criticalVoltage = storage.getSettings().getCriticalVoltage();

    // Determine battery status using standard thresholds
    if (voltage >= BATTERY_VOLTAGE_GOOD) {
        status = BatteryStatus::GOOD;
    } else if (voltage >= BATTERY_VOLTAGE_OKAY) {
        status = BatteryStatus::OKAY;
    } else if (voltage >= BATTERY_VOLTAGE_LOW) {
        status = BatteryStatus::LOW_BATTERY;
    } else if (voltage >= criticalVoltage) {
        status = BatteryStatus::LOW_BATTERY;  // Still low, but not critical yet
    } else {
        status = BatteryStatus::CRITICAL;
    }

    // Detect charging: voltage above charging threshold
    // Only consider charging if voltage is in valid battery range (>10V) and above threshold
    charging = (voltage >= 10.0f) && (voltage >= BATTERY_CHARGING_THRESHOLD);
}

const char* Battery::getStatusText() const {
    switch (status) {
        case BatteryStatus::GOOD:
            return "Good";
        case BatteryStatus::OKAY:
            return "Okay";
        case BatteryStatus::LOW_BATTERY:
            return "Low";
        case BatteryStatus::CRITICAL:
            return "Critical";
        default:
            return "???";
    }
}
