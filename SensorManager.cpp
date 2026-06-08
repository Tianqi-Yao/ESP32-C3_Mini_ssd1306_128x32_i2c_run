#include "SensorManager.h"
#include "StorageManager.h"  // for the LOG macro
#include <Adafruit_SHT31.h>

// SHT35 object
static Adafruit_SHT31 sht31 = Adafruit_SHT31();

// Cached current temperature/humidity values
static float currentTemp = NAN;
static float currentHum = NAN;
static bool sensorReady = false; // whether initialization succeeded

// Initialize the sensor (the main program must call Wire.begin first).
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

// Refresh sensor data once; returns whether it succeeded.
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

// Get the current temperature/humidity values.
float getTemperature() { return currentTemp; }
float getHumidity()    { return currentHum; }
