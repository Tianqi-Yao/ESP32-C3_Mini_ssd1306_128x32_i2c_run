#include "SensorManager.h"
#include "StorageManager.h"  // 👈 引入 LOG 宏
#include <Adafruit_SHT31.h>

// SHT35 对象
static Adafruit_SHT31 sht31 = Adafruit_SHT31();

// 当前温湿度缓存值
static float currentTemp = NAN;
static float currentHum = NAN;
static bool sensorReady = false; // 标记是否初始化成功

// 初始化传感器（主程序需先调用 Wire.begin）
bool sensorInit()
{
    if (!sht31.begin(0x44)) {
        LOG("SHT35 initialization failed.");
        sensorReady = false;
        return false;
    }
    sensorReady = true;
    LOG("SHT35 sensor ready.");
    return true;
}

// 刷新一次传感器数据，返回是否成功
bool refreshSensorData()
{
    if (!sensorReady) {
        LOG("Sensor not ready. Skipping read.");
        return false;
    }

    float t = sht31.readTemperature();
    float h = sht31.readHumidity();

    if (isnan(t) || isnan(h)) {
        LOG("Failed to read SHT35 data (NaN).");
        return false;
    }

    currentTemp = t;
    currentHum = h;

    LOG("sensor: Temp: " + String(t, 2) + " C, Hum: " + String(h, 2) + " %");
    return true;
}

// 获取当前温湿度值
float getTemperature() { return currentTemp; }
float getHumidity()    { return currentHum; }
