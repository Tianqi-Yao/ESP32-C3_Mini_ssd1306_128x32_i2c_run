#include "BatteryMonitor.h"
#include "StorageManager.h" // 👈 引入 LOG 宏
#include <Arduino.h>

const int BATTERY_ADC_PIN = 0;         // GPIO0 = ADC1_CH0（实际接线引脚）
const float VOLTAGE_DIVIDER_RATIO = 43.0 / 10.0;  // 上臂33k + 下臂10k 分压，比值 4.3

// 4S LiFePO4（“12V”磷酸铁锂，4×3.2V 标称）静置开路电压 -> 剩余电量% 分段表。
// 注意：LiFePO4 放电曲线极平，中段(20%~90%)电压几乎不变，仅靠电压估 SOC 在中段
// 本就不精确；且应以静置电压为准（带负载时电压下垂会偏低）。表值由高到低，线性插值。
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

// ADC采样优化-使用多次平均值（滤掉抖动）。
// analogReadMilliVolts 内部已套用 eFuse 出厂校准，返回管脚处实测毫伏。
float readADC_AveragedMillivolts(int pin, int samples = 16) {
    long sum = 0;
    for (int i = 0; i < samples; ++i) {
        sum += analogReadMilliVolts(pin);
        delayMicroseconds(1000);  // 稳定性更高
    }
    return (float)sum / samples;
}

float readBatteryVoltage() {
    float mv = readADC_AveragedMillivolts(BATTERY_ADC_PIN, 16); // 16次平均值（管脚处毫伏）
    float voltage12v = (mv / 1000.0) * VOLTAGE_DIVIDER_RATIO;   // 还原分压前的实际电压
    return voltage12v;
}

// 由已读到的电压估算电量，避免重复触发 ADC 采样
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

// 便捷封装：自行读取一次电压再估算（保留旧调用方式）
int getBatteryPercentage() {
    return getBatteryPercentage(readBatteryVoltage());
}
