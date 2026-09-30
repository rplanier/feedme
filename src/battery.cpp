#include "battery.h"
#include "storage.h"
#include "esp_adc/adc_cali_scheme.h"

Battery battery;

void Battery::begin() {
    // Configure ADC pins
    pinMode(PIN_BATTERY_ADC, INPUT);
    pinMode(PIN_SOLAR_ADC, INPUT);

    // Set ADC resolution to 12 bits
    analogReadResolution(12);

    // 12 dB attenuation gives an effective measurement range of 0-3300 mV
    // (ESP32-C6 datasheet Table 5-6).  Both sense pins are on ADC1
    // (GPIO0 = ADC1_CH0, GPIO1 = ADC1_CH1), so they stay usable with the radio
    // active -- ADC2 would not.  This is the Arduino default, set explicitly so
    // a core default change cannot silently rescale every reading.
    // analogSetPinAttenuation() is a no-op on a pin the core has not yet
    // attached to the ADC (it logs "Pin is not configured as analog channel").
    // Take one throwaway reading of each pin first so the calls below stick.
    analogRead(PIN_BATTERY_ADC);
    analogRead(PIN_SOLAR_ADC);
    analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);
    analogSetPinAttenuation(PIN_SOLAR_ADC, ADC_11db);
    using6dB = false;

    initCalibration();

    refillSamples(10);
    previousVoltage = voltage;

    // Now that we know roughly what pack is fitted, pick the right attenuation
    // and re-fill the buffer -- the samples above were taken at 12 dB.
    updateAttenuation();

    updateStatus();
}

void Battery::refillSamples(uint32_t interSampleDelayMs) {
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        samples[i] = readRawVoltage();
        delay(interSampleDelayMs);
    }
    sampleIndex = 0;
    float sum = 0;
    for (int i = 0; i < SAMPLE_COUNT; i++) sum += samples[i];
    voltage = sum / SAMPLE_COUNT;
}

void Battery::update() {
    uint32_t now = millis();
    if (now - lastReadTime < READ_INTERVAL_MS) {
        return;
    }
    lastReadTime = now;
    if (samplingPaused) {
        return;  // motor running: keep the resting value, see setSamplingPaused()
    }

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

    updateAttenuation();
    updateStatus();
}

void Battery::applyAttenuation(bool use6dB) {
    if (use6dB == using6dB) return;
    using6dB = use6dB;
    analogSetPinAttenuation(PIN_BATTERY_ADC, use6dB ? ADC_6db : ADC_11db);
    DEBUG_PRINTF("Battery: ADC attenuation -> %s\n", use6dB ? "6dB (6V pack)" : "12dB (12V pack)");

    // Every sample in the buffer was taken at the old attenuation, so they are
    // meaningless now.  Re-fill rather than let stale values skew the average.
    refillSamples(2);
}

void Battery::updateAttenuation() {
    // Hysteresis band sits between a 6 V pack's charging ceiling (~7.4 V) and a
    // 12 V pack's critical floor (10.8 V), so neither can oscillate.
    // Never select 6 dB without a 6 dB calibration handle: the core fallback
    // would convert with the 12 dB curve and read ~1.9x high.
    if (!using6dB && cali6dB != nullptr && voltage > 0.5f && voltage < ADC_ATTEN_SWITCH_TO_6DB) {
        applyAttenuation(true);
    } else if (using6dB && (voltage > ADC_ATTEN_SWITCH_TO_12DB || batteryRawSaturated)) {
        // Raw saturation is the belt to the voltage threshold's braces: a 12 V
        // pack at 6 dB clips near 9.6-9.9 V, only ~0.2 V (at the pin) above the
        // release threshold, so also release on the raw count itself.
        applyAttenuation(false);
    }
}

void Battery::initCalibration() {
    // Both sense pins are on ADC1.  Curve fitting is the scheme the ESP32-C6
    // supports; the factory eFuse data it reads is per attenuation, hence one
    // handle each.
    adc_cali_curve_fitting_config_t cfg = {};
    cfg.unit_id = ADC_UNIT_1;
    cfg.bitwidth = ADC_BITWIDTH_12;

    cfg.atten = ADC_ATTEN_DB_12;
    if (adc_cali_create_scheme_curve_fitting(&cfg, &cali12dB) != ESP_OK) {
        cali12dB = nullptr;
        DEBUG_PRINTLN("Battery: 12 dB ADC calibration unavailable, falling back to core");
    }
    cfg.atten = ADC_ATTEN_DB_6;
    if (adc_cali_create_scheme_curve_fitting(&cfg, &cali6dB) != ESP_OK) {
        cali6dB = nullptr;
        DEBUG_PRINTLN("Battery: 6 dB ADC calibration unavailable, falling back to core");
    }
}

uint32_t Battery::readMilliVolts(uint8_t pin, bool sixDb) {
    adc_cali_handle_t handle = sixDb ? cali6dB : cali12dB;
    if (handle == nullptr) {
        // Core conversion, which uses the unit-wide 12 dB curve.  Only reached
        // at 12 dB: updateAttenuation() refuses 6 dB when cali6dB is missing.
        return analogReadMilliVolts(pin);
    }
    int raw = analogRead(pin);  // 12-bit counts at the channel's own attenuation
    if (pin == PIN_BATTERY_ADC) {
        batteryRawSaturated = raw >= 4000;
    }
    int mv = 0;
    if (adc_cali_raw_to_voltage(handle, raw, &mv) != ESP_OK) {
        return analogReadMilliVolts(pin);
    }
    return (uint32_t)mv;
}

float Battery::readInstantVoltage() {
    // Deliberately a single conversion: stall detection needs speed, not
    // precision, and the caller averages over consecutive samples instead.
    return (readMilliVolts(PIN_BATTERY_ADC, using6dB) / 1000.0f) * BATTERY_DIVIDER_RATIO;
}

float Battery::readRawVoltage() {
    // Four calibrated reads averaged for noise.  readMilliVolts() converts raw
    // counts with the curve-fitting handle for the channel's current attenuation.
    uint32_t mvSum = 0;
    for (int i = 0; i < 4; i++) {
        mvSum += readMilliVolts(PIN_BATTERY_ADC, using6dB);
        delayMicroseconds(100);
    }
    float adcVoltage = (mvSum / 4) / 1000.0f;

    // Apply voltage divider ratio to get actual battery voltage
    float batteryVoltage = adcVoltage * BATTERY_DIVIDER_RATIO;

    // Debug output every 10 seconds (controlled by caller)
    static uint32_t lastDebugPrint = 0;
    if (millis() - lastDebugPrint >= 10000) {
        lastDebugPrint = millis();
        DEBUG_PRINTF("Battery: adcMV=%u, adcV=%.3f, battV=%.2f\n",
                      (unsigned)(mvSum / 4), adcVoltage, batteryVoltage);
    }

    return batteryVoltage;
}

float Battery::readSolarVoltage() {
    // Four calibrated reads averaged, always at 12 dB (a 6 V panel still reaches
    // ~9.8 V open-circuit, outside the 6 dB range).  SOLAR_CHARGING_THRESHOLD keys
    // off this value, so it must be calibrated, not scaled by a fudge.
    uint32_t mvSum = 0;
    for (int i = 0; i < 4; i++) {
        mvSum += readMilliVolts(PIN_SOLAR_ADC, false);  // solar stays at 12 dB
        delayMicroseconds(100);
    }
    float adcVoltage = (mvSum / 4) / 1000.0f;

    // Apply voltage divider ratio to get actual solar panel voltage.
    // The solar divider (R6/R5) is identical to the battery divider (R8/R7).
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
