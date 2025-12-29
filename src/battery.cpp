#include "battery.h"

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

    // Calculate smoothed voltage
    float sum = 0;
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        sum += samples[i];
    }
    voltage = sum / SAMPLE_COUNT;

    updateStatus();
}

float Battery::readRawVoltage() {
    // Read ADC value
    int rawValue = analogRead(PIN_BATTERY_ADC);

    // Convert to voltage at ADC pin
    float adcVoltage = (rawValue / (float)ADC_MAX_VALUE) * ADC_REFERENCE_VOLTAGE;

    // Apply voltage divider ratio to get actual battery voltage
    float batteryVoltage = adcVoltage * BATTERY_DIVIDER_RATIO;

    return batteryVoltage;
}

void Battery::updateStatus() {
    // Determine battery status
    if (voltage >= BATTERY_VOLTAGE_GOOD) {
        status = BatteryStatus::GOOD;
    } else if (voltage >= BATTERY_VOLTAGE_OKAY) {
        status = BatteryStatus::OKAY;
    } else if (voltage >= BATTERY_VOLTAGE_CRITICAL) {
        status = BatteryStatus::LOW_BATTERY;
    } else {
        status = BatteryStatus::CRITICAL;
    }

    // Detect charging: voltage above charging threshold or rising significantly
    charging = (voltage >= BATTERY_CHARGING_THRESHOLD) ||
               (voltage > previousVoltage + 0.05f);  // Rising by >50mV
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
            return "CRIT";
        default:
            return "???";
    }
}
