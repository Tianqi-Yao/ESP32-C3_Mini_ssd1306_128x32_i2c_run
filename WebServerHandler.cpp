#include "WebServerHandler.h"
#include "StorageManager.h"     // for the LOG macro
#include "SensorManager.h"
#include "BatteryMonitor.h"
#include "PowerLatch.h"
#include "RTC.h"
#include "WebUI.h"
#include <ArduinoJson.h>
#include <FS.h>
#include <SD.h>
#include <WiFi.h>
#include <WebServer.h>
#include "esp_task_wdt.h"

const char* ssid = "ESP32-AP";
const char* password = "12345678";
WebServer server(80);
bool exitRequested = false;

// Path whitelist: only allow access to files under /logs/, and reject directory traversal.
static bool isAllowedPath(const String& p) {
    return p.startsWith("/logs/") && p.indexOf("..") < 0;
}

// Stream a file to the client in chunks, feeding the watchdog between chunks so a large file
// or a slow link cannot trip the task watchdog mid-transfer and reset the device.
static void streamFileFed(File& file, const char* contentType) {
    server.setContentLength(file.size());
    server.send(200, contentType, "");
    uint8_t buf[1024];
    while (file.available()) {
        size_t n = file.read(buf, sizeof(buf));
        if (n == 0) break;
        server.sendContent((const char*)buf, n);
        esp_task_wdt_reset();
    }
}

void handleFileDownload() {
    if (!isStorageReady()) { server.send(503, "text/plain", "SD card not ready"); return; }
    if (!server.hasArg("path")) {
        server.send(400, "text/plain", "Missing 'path' parameter");
        return;
    }

    String path = server.arg("path");
    if (!isAllowedPath(path)) {
        server.send(403, "text/plain", "Path not allowed");
        LOG("WebServer: rejected unauthorized download: " + path);
        return;
    }
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) file.close();
        server.send(404, "text/plain", "File not found or is a directory");
        return;
    }

    streamFileFed(file, "application/octet-stream");
    file.close();
}


void listFilesRecursively(File dir, JsonArray arr, String path = "") {
    while (true) {
        esp_task_wdt_reset();
        File entry = dir.openNextFile();
        if (!entry) break;

        String rawName = entry.name();
        // Newer SD library versions return a full path (e.g. "/logs/file.csv") rather than a bare
        // name; extract the final component so that name and fullPath are always consistent.
        int slashIdx = rawName.lastIndexOf('/');
        String name = (slashIdx >= 0) ? rawName.substring(slashIdx + 1) : rawName;
        // Skip hidden/metadata entries (macOS .Spotlight-V100, .fseventsd, ._*, .DS_Store, etc.)
        // Check is done on the extracted basename so it works for both bare names and full paths.
        if (name.startsWith(".")) {
            entry.close();
            continue;
        }

        JsonObject obj = arr.add<JsonObject>();
        String fullPath = path + "/" + name;
        obj["name"] = name;
        obj["path"] = fullPath;
        obj["type"] = entry.isDirectory() ? "dir" : "file";

        if (entry.isDirectory()) {
            JsonArray children = obj["children"].to<JsonArray>();
            listFilesRecursively(entry, children, fullPath);
        }
        entry.close();
    }
}

void handleListFiles() {
    if (!isStorageReady()) { server.send(503, "text/plain", "SD card not ready"); return; }
    JsonDocument doc;  // 7.x elastic document, grows as needed
    JsonArray root = doc.to<JsonArray>();

    // List only /logs (the only readable/downloadable location per isAllowedPath), so the tree
    // does not show files outside the whitelist that would 403 when clicked.
    File logsDir = SD.open("/logs");
    if (!logsDir || !logsDir.isDirectory()) {
        server.send(500, "text/plain", "Failed to open /logs directory");
        LOG("WebServer: failed to open /logs directory");
        return;
    }
    listFilesRecursively(logsDir, root, "/logs");
    logsDir.close();

    String result;
    serializeJson(doc, result);
    server.send(200, "application/json", result);
}

void handleReadFile() {
    if (!isStorageReady()) { server.send(503, "text/plain", "SD card not ready"); return; }
    if (!server.hasArg("path")) {
        server.send(400, "text/plain", "Missing 'path' parameter");
        LOG("WebServer: Missing 'path' parameter for file read.");
        return;
    }

    String path = server.arg("path");
    if (!isAllowedPath(path)) {
        server.send(403, "text/plain", "Path not allowed");
        LOG("WebServer: rejected unauthorized read: " + path);
        return;
    }
    File file = SD.open(path);
    if (!file || file.isDirectory()) {
        if (file) file.close();
        server.send(404, "text/plain", "File not found or is a directory");
        LOG("WebServer: Invalid file or directory requested: " + path);
        return;
    }

    // Stream the response (chunked, watchdog-fed) to avoid building the whole file into a String.
    streamFileFed(file, "text/plain");
    file.close();
}

void handleSDReinit() {
    bool ok = storageReinit();
    server.send(200, "text/plain", ok ? "SD remounted OK" : "SD remount failed");
    if (ok) LOG("SD remount triggered via web.");
    else    Serial.println("SD remount failed via web.");
}

void handleRoot() {
    float temp = getTemperature();
    float hum = getHumidity();
    float batteryVoltage = readBatteryVoltage();
    int batteryPercent = getBatteryPercentage(batteryVoltage);
    String timeStr = isRtcValid() ? getRTCTimeString() : String("N/A");
    timeStr.replace("&", "&amp;");
    timeStr.replace("<", "&lt;");
    timeStr.replace(">", "&gt;");
    bool sdReady = isStorageReady();
    String html = sensorUI(temp, hum, batteryVoltage, batteryPercent, timeStr, sdReady);
    server.send(200, "text/html", html);
}

void handleExit() {
    exitRequested = true;
    server.send(204);
    LOG("Exit request received via WebUI.");
}

void handlePowerOn() {
    server.send(204);   // respond first so the browser doesn't wait through the relay delay
    powerOn();
}

void handlePowerOff() {
    server.send(204);
    powerOff();
}

void handleRTCSyncFromBrowser() {
    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "Missing time data");
        LOG("RTC sync failed: missing body data.");
        return;
    }

    String body = server.arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        server.send(400, "text/plain", "JSON parse failed");
        LOG("RTC sync failed: JSON parse error.");
        return;
    }

    String isoTime = doc["time"];
    struct tm tmInfo;
    if (sscanf(isoTime.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d",
                &tmInfo.tm_year, &tmInfo.tm_mon, &tmInfo.tm_mday,
                &tmInfo.tm_hour, &tmInfo.tm_min, &tmInfo.tm_sec) == 6)
    {
        int year = tmInfo.tm_year;
        int mon  = tmInfo.tm_mon;
        static const int maxDay[] = {0,31,29,31,30,31,30,31,31,30,31,30,31};
        bool isLeap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        int dayLimit = (mon == 2 && !isLeap) ? 28 : maxDay[mon];
        if (year < 2020 || year > 2099 ||
            mon  < 1    || mon  > 12   ||
            tmInfo.tm_mday < 1 || tmInfo.tm_mday > dayLimit ||
            tmInfo.tm_hour < 0 || tmInfo.tm_hour > 23 ||
            tmInfo.tm_min  < 0 || tmInfo.tm_min  > 59 ||
            tmInfo.tm_sec  < 0 || tmInfo.tm_sec  > 59) {
            server.send(400, "text/plain", "Time values out of range");
            LOG("RTC sync failed: time values out of range.");
            return;
        }
        DateTime dt(year, mon, tmInfo.tm_mday,
                    tmInfo.tm_hour, tmInfo.tm_min, tmInfo.tm_sec);
        setRTC(dt);
        server.send(200, "text/plain", "RTC synchronized with phone time");
        LOG("RTC synchronized from browser.");
    } else {
        server.send(400, "text/plain", "Invalid time format");
        LOG("RTC sync failed: invalid time format.");
    }
}

void handleFormatSDCard() {
    if (!isStorageReady()) { server.send(503, "text/plain", "SD card not ready"); return; }
    File dir = SD.open("/logs");
    if (!dir || !dir.isDirectory()) {
        server.send(500, "text/plain", "Failed to open log directory");
        LOG("Failed to clear SD card: /logs missing or cannot be opened");
        return;
    }

    // Collect all file paths first, then close the directory before deleting.
    // Mutating a directory while iterating it can cause the iterator to skip entries
    // or read stale FAT data on some SD library versions.
    // entry.name() may return a bare name or a full path depending on SD library version;
    // use it as-is if it starts with '/', otherwise prepend /logs/.
    const int MAX_LOG_FILES = 200;
    String toDelete[MAX_LOG_FILES];
    int fileCount = 0;
    while (fileCount < MAX_LOG_FILES) {
        File entry = dir.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) {
            String n = entry.name();
            toDelete[fileCount++] = n.startsWith("/") ? n : (String("/logs/") + n);
        }
        entry.close();
    }
    File extra = dir.openNextFile();
    bool truncated = (fileCount == MAX_LOG_FILES) && extra;
    if (extra) extra.close();
    dir.close();

    int deleted = 0;
    for (int i = 0; i < fileCount; i++) {
        esp_task_wdt_reset();
        if (SD.remove(toDelete[i])) {
            ++deleted;
            Serial.println("Deleted: " + toDelete[i]);
        } else {
            Serial.println("Delete failed: " + toDelete[i]);
        }
    }

    String msg = "Logs cleared, deleted " + String(deleted) + " file(s)";
    if (truncated) msg += " (WARNING: too many files, some not deleted — run again)";
    LOG(msg);
    server.send(200, "text/plain", msg);
}



void startWiFiAndWeb() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, password);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    // Register routes only once: server.stop() does not clear the handler list, so registering
    // on every WiFi-mode entry would leak a duplicate set of handlers each time.
    static bool routesRegistered = false;
    if (!routesRegistered) {
        server.on("/", handleRoot);
        server.on("/exit",      HTTP_POST, handleExit);
        server.on("/power-on",  HTTP_POST, handlePowerOn);
        server.on("/power-off", HTTP_POST, handlePowerOff);
        server.on("/rtc-sync-browser", HTTP_POST, handleRTCSyncFromBrowser);
        server.on("/files",    handleListFiles);
        server.on("/file",     handleReadFile);
        server.on("/format-sd", HTTP_POST, handleFormatSDCard);
        server.on("/download", handleFileDownload);
        server.on("/sd-reinit", HTTP_POST, handleSDReinit);
        routesRegistered = true;
    }
    server.begin();
    exitRequested = false;

    LOG("WiFi AP started at: http://192.168.4.1");
}

void stopWiFiAndWeb() {
    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    exitRequested = false;
    LOG("WiFi and Web server stopped.");
}

void handleClientRequests() {
    server.handleClient();
}

bool shouldExitWiFiMode() {
    return exitRequested;
}

void blinkLED(int LED_PIN) {
    static unsigned long lastToggle = 0;
    static bool ledState = false;
    unsigned long t = millis();
    if (t - lastToggle > 500) {
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState ? HIGH : LOW);
        lastToggle = t;
    }
}
