#include "BatteryMonitor.h"
#include "StorageManager.h" // 👈 引入 LOG 宏
#include <Arduino.h>

const int BATTERY_ADC_PIN = 0;         // A1 = GPIO3
const float ADC_VREF = 3.0;            // ESP32 ADC参考电压
const int ADC_MAX = 4095;              // 12位ADC
const float VOLTAGE_DIVIDER_RATIO = 84.0 / 20.0;  // 64k + 20k 分压

const float BATTERY_FULL = 12.6;       // 满电电压
const float BATTERY_EMPTY = 9.0;       // 空电电压

// ADC采样优化-使用多次平均值（滤掉抖动）
float readADC_Averaged(int pin, int samples = 16) {
    long sum = 0;
    for (int i = 0; i < samples; ++i) {
        sum += analogRead(pin);
        delayMicroseconds(1000);  // 稳定性更高
    }
    return (float)sum / samples;
}

float readBatteryVoltage() {
    float raw = readADC_Averaged(BATTERY_ADC_PIN, 16); // 16次平均值
    float voltage = raw * ADC_VREF / ADC_MAX; // 计算实际电压
    float voltage12v = voltage * VOLTAGE_DIVIDER_RATIO; // 分压计算

    // char buf[128];
    // snprintf(buf, sizeof(buf), "Raw ADC: %.2f, Voltage: %.2fV, Voltage12V: %.2fV",
    //          raw, voltage, voltage12v);
    // LOG(buf);

    return voltage12v; // 返回实际电压
}

int getBatteryPercentage() {
    float voltage = readBatteryVoltage();
    int percent = (voltage - BATTERY_EMPTY) / (BATTERY_FULL - BATTERY_EMPTY) * 100;
    percent = constrain(percent, 0, 100);
    return percent;
}
