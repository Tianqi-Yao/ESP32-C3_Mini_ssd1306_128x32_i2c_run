#include "StorageManager.h"
#include "RTC.h"
#include <SPI.h>
#include <SD.h>
#include "esp_task_wdt.h"

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

bool storageReinit()
{
    esp_task_wdt_reset();   // SD.end() may take time; feed watchdog before blocking ops
    SD.end();
    spi.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SD_CS);
    esp_task_wdt_reset();   // SD.begin() may take several seconds; feed again
    if (!SD.begin(SD_CS, spi)) {
        storageReady = false;
        Serial.println("SD remount failed.");
        return false;
    }
    SD.mkdir(LOG_DIR);
    storageReady = true;
    Serial.println("SD card remounted successfully.");
    return true;
}

bool isStorageReady()
{
    return storageReady;
}

bool storageHealthCheck()
{
    esp_task_wdt_reset();   // SD.open() on a missing card may stall; feed watchdog first
    File dir = SD.open("/logs");
    bool ok = dir && dir.isDirectory();
    if (dir) dir.close();
    if (!ok) {
        Serial.println("SD health check failed, attempting remount...");
        return storageReinit();
    }
    storageReady = true;
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

    String logPath = getTodayLogPath();
    File file = SD.open(logPath.c_str(), FILE_APPEND);
    if (!file) {
        Serial.println("SD open failed, attempting remount...");
        if (!storageReinit()) return false;
        file = SD.open(logPath.c_str(), FILE_APPEND);
        if (!file) {
            Serial.println("Failed to open log file after remount.");
            return false;
        }
    }

    file.println(line);
    file.close();
    return true;
}

bool logMessage(const String& message)
{
    if (!storageReady) {
        Serial.println("Cannot log message, SD not ready.");
        return false;
    }

    bool rtcOk = isRtcValid();
    DateTime now;
    if (rtcOk) now = getCurrentDateTime();

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
        Serial.println("SD open failed, attempting remount...");
        if (!storageReinit()) return false;
        file = SD.open(filename, FILE_APPEND);
        if (!file) {
            Serial.println("Failed to open system log file after remount.");
            return false;
        }
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
