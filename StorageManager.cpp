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
    // Fall back to a fixed name when the RTC time is invalid, so a dead RTC does not
    // scatter hundreds of garbage-dated files onto the card.
    if (!isRtcValid()) {
        return String(LOG_DIR) + "/unknown-date.csv";
    }
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

    char line[64];
    if (isRtcValid()) {
        DateTime now = getCurrentDateTime();
        snprintf(line, sizeof(line), "%04d-%02d-%02d,%02d:%02d:%02d,%.2f,%.2f",
                now.year(), now.month(), now.day(),
                now.hour(), now.minute(), now.second(),
                temp, hum);
    } else {
        // RTC time invalid: write zeroed timestamp columns instead of garbage
        snprintf(line, sizeof(line), "0000-00-00,00:00:00,%.2f,%.2f", temp, hum);
    }

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

    bool rtcOk = isRtcValid();
    DateTime now = getCurrentDateTime();

    // Name the log file by date, falling back to a fixed name when the RTC time is invalid.
    char filename[40];
    if (rtcOk) {
        snprintf(filename, sizeof(filename), "/logs/systemLog_%04d-%02d-%02d.txt",
                 now.year(), now.month(), now.day());
    } else {
        snprintf(filename, sizeof(filename), "/logs/systemLog_unknown.txt");
    }

    File file = SD.open(filename, FILE_APPEND);
    if (!file) {
        Serial.println("Failed to open system log file.");
        return false;
    }

    // Write a timestamped log line (zeros when the RTC time is invalid)
    char timestamp[32];
    if (rtcOk) {
        snprintf(timestamp, sizeof(timestamp), "[%02d:%02d:%02d]",
                 now.hour(), now.minute(), now.second());
    } else {
        snprintf(timestamp, sizeof(timestamp), "[00:00:00]");
    }

    file.print(timestamp);
    file.print(" ");
    file.println(message);
    file.close();
    return true;
}
