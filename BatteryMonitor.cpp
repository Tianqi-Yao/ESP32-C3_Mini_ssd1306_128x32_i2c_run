#include "BatteryMonitor.h"
#include "StorageManager.h" // for the LOG macro
#include <Arduino.h>

const int BATTERY_ADC_PIN = 0;         // GPIO0 = ADC1_CH0 (actual wiring pin)
// Physical divider is 33k/10k = 4.33, but near full charge the tap voltage (~3.1V) exceeds the
// ESP32-C3 ADC's reliable linear range and the ADC reads low (high-end compression).
// Multimeter calibration at full charge: true battery 13.42V, true tap 3.097V, but the ADC
// reports only 2.912V (~0.19V low). So the ratio is set to the empirical 13.42/2.912 = 4.61,
// anchoring the full-charge end (the LiFePO4 operating band is narrow).
// Note: this single-point calibration may read slightly high at the low end, so cutoff triggers a
// little late (fine with a BMS). For full-range accuracy add a low-charge point (two-point cal);
// the clean hardware fix is lower resistor 10k -> 6.8k (ratio ~5.85, tap <2.5V). See README.
const float VOLTAGE_DIVIDER_RATIO = 4.61;

// 4S LiFePO4 ("12V" lithium iron phosphate, 4x3.2V nominal): resting open-circuit voltage -> SOC% table.
// Note: the LiFePO4 discharge curve is very flat; in the mid range (20%-90%) the voltage barely
// changes, so voltage-based SOC is inherently imprecise there. Readings should be taken at rest
// (voltage sags under load and reads low). Entries are high-to-low; values are linearly interpolated.
struct SocPoint { float voltage; int percent; };
static const SocPoint SOC_TABLE[] = {
    {13.60, 100},
    {13.40, 99},
    {13.33, 90},
    {13.20, 70},
    {13.10, 40},
    {13.00, 30},
    {12.90, 20},
    {12.80, 17},
    {12.50, 14},
    {12.00, 9},
    {10.00, 0},
};
static const int SOC_TABLE_LEN = sizeof(SOC_TABLE) / sizeof(SOC_TABLE[0]);

// Averaged ADC sampling (filters out jitter).
// analogReadMilliVolts applies the factory eFuse calibration internally and returns the
// measured millivolts at the pin.
float readADC_AveragedMillivolts(int pin, int samples = 16) {
    long sum = 0;
    for (int i = 0; i < samples; ++i) {
        sum += analogReadMilliVolts(pin);
        delayMicroseconds(1000);  // improves stability
    }
    return (float)sum / samples;
}

float readBatteryVoltage() {
    float mv = readADC_AveragedMillivolts(BATTERY_ADC_PIN, 16); // average of 16 samples (millivolts at pin)
    float voltage12v = (mv / 1000.0) * VOLTAGE_DIVIDER_RATIO;   // undo the divider to get the real voltage
    return voltage12v;
}

// Estimate SOC from an already-read voltage, avoiding a redundant ADC sample.
int getBatteryPercentage(float voltage) {
    if (voltage >= SOC_TABLE[0].voltage) return SOC_TABLE[0].percent;
    if (voltage <= SOC_TABLE[SOC_TABLE_LEN - 1].voltage) return SOC_TABLE[SOC_TABLE_LEN - 1].percent;

    for (int i = 0; i < SOC_TABLE_LEN - 1; ++i) {
        float vHi = SOC_TABLE[i].voltage;
        float vLo = SOC_TABLE[i + 1].voltage;
        if (voltage <= vHi && voltage >= vLo) {
            float ratio = (voltage - vLo) / (vHi - vLo);
            float percent = SOC_TABLE[i + 1].percent +
                            ratio * (SOC_TABLE[i].percent - SOC_TABLE[i + 1].percent);
            return constrain((int)(percent + 0.5), 0, 100);
        }
    }
    return 0;
}

// Convenience wrapper: read the voltage once and estimate (keeps the old call style).
int getBatteryPercentage() {
    return getBatteryPercentage(readBatteryVoltage());
}
