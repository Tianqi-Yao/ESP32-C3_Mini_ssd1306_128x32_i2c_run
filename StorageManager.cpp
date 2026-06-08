#include "StorageManager.h"
#include "RTC.h"
#include <SPI.h>
#include <SD.h>

#define SD_CS         4
#define LOG_DIR       "/logs"
#define MISO_PIN      1
#define MOSI_PIN      3
#define SCK_PIN       2
#define STORAGE_DEBUG 1

static SPIClass spi = SPIClass(FSPI);
static bool storageReady = false;

bool storageInit()
{
    spi.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SD_CS);

    if (!SD.begin(SD_CS, spi)) {
#if STORAGE_DEBUG
        LOG("SD card initialization failed.");
#endif
        return false;
    }

    SD.mkdir(LOG_DIR);
    storageReady = true;
#if STORAGE_DEBUG
    LOG("SD card ready.");
#endif
    return true;
}

String getTodayLogPath()
{
    DateTime now = getCurrentDateTime();
    char buf[32];
    snprintf(buf, sizeof(buf), "%s/%04d-%02d-%02d.csv",
            LOG_DIR, now.year(), now.month(), now.day());
    return String(buf);
}

bool saveTemperatureHumidityWithTime(float temp, float hum)
{
    if (!storageReady || isnan(temp) || isnan(hum)) {
#if STORAGE_DEBUG
        LOG("Skip saving: SD not ready or invalid data.");
#endif
        return false;
    }

    DateTime now = getCurrentDateTime();
    char line[64];
    snprintf(line, sizeof(line), "%04d-%02d-%02d,%02d:%02d:%02d,%.2f,%.2f",
            now.year(), now.month(), now.day(),
            now.hour(), now.minute(), now.second(),
            temp, hum);

    File file = SD.open(getTodayLogPath().c_str(), FILE_APPEND);
    if (!file) {
#if STORAGE_DEBUG
        LOG("Failed to open log file.");
#endif
        return false;
    }

    file.println(line);
    file.close();

#if STORAGE_DEBUG
    LOG("Data saved: " + String(line));
#endif

    return true;
}

bool logMessage(const String& message)
{
    if (!storageReady) {
        Serial.println("Cannot log message, SD not ready.");
        return false;
    }

    DateTime now = getCurrentDateTime();

    // Name the log file by date
    char filename[32];
    snprintf(filename, sizeof(filename), "/logs/systemLog_%04d-%02d-%02d.txt",
             now.year(), now.month(), now.day());

    File file = SD.open(filename, FILE_APPEND);
    if (!file) {
        Serial.println("Failed to open system log file.");
        return false;
    }

    // Write a timestamped log line
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%02d:%02d:%02d]",
             now.hour(), now.minute(), now.second());

    file.print(timestamp);
    file.print(" ");
    file.println(message);
    file.close();
    return true;
}
