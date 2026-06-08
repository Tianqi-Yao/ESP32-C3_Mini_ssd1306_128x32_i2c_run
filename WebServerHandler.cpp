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

const char* ssid = "ESP32-AP";
const char* password = "12345678";
WebServer server(80);
bool exitRequested = false;

// Path whitelist: only allow access to files under /logs/, and reject directory traversal.
static bool isAllowedPath(const String& p) {
    return p.startsWith("/logs/") && p.indexOf("..") < 0;
}

void handleFileDownload() {
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
        server.send(404, "text/plain", "File not found or is a directory");
        return;
    }

    server.streamFile(file, "application/octet-stream");
    file.close();
}


void listFilesRecursively(File dir, JsonArray arr, String path = "") {
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;

        String name = entry.name();
        // Skip hidden/metadata entries (macOS .Spotlight-V100, .fseventsd, ._*, .DS_Store, etc.)
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
    JsonDocument doc;  // 7.x elastic document, grows as needed
    JsonArray root = doc.to<JsonArray>();

    File rootDir = SD.open("/");
    if (!rootDir) {
        server.send(500, "text/plain", "Failed to open SD root directory");
        LOG("WebServer: failed to open SD root directory");
        return;
    }
    listFilesRecursively(rootDir, root);
    rootDir.close();

    String result;
    serializeJson(doc, result);
    server.send(200, "application/json", result);
}

void handleReadFile() {
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
        server.send(404, "text/plain", "File not found or is a directory");
        LOG("WebServer: Invalid file or directory requested: " + path);
        return;
    }

    // Stream the response to avoid building the whole file into a String and exhausting memory.
    server.streamFile(file, "text/plain");
    file.close();
}

void handleRoot() {
    float temp = getTemperature();
    float hum = getHumidity();
    float batteryVoltage = readBatteryVoltage();
    int batteryPercent = getBatteryPercentage(batteryVoltage);
    String timeStr = getRTCTimeString();
    String html = sensorUI(temp, hum, batteryVoltage, batteryPercent, timeStr);
    server.send(200, "text/html", html);
}

void handleExit() {
    exitRequested = true;
    server.send(204);
    LOG("Exit request received via WebUI.");
}

void handlePowerOn() {
    powerOn();
    server.send(204);
}

void handlePowerOff() {
    powerOff();
    server.send(204);
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
    struct tm tm;
    if (sscanf(isoTime.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d",
                &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6)
    {
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        DateTime dt(
            tm.tm_year + 1900,
            tm.tm_mon + 1,
            tm.tm_mday,
            tm.tm_hour,
            tm.tm_min,
            tm.tm_sec
        );
        setRTC(dt);
        server.send(200, "text/plain", "RTC synchronized with phone time");
        LOG("RTC synchronized from browser.");
    } else {
        server.send(400, "text/plain", "Invalid time format");
        LOG("RTC sync failed: invalid time format.");
    }
}

void handleFormatSDCard() {
    File dir = SD.open("/logs");
    if (!dir || !dir.isDirectory()) {
        server.send(500, "text/plain", "Failed to open log directory");
        LOG("Failed to clear SD card: /logs missing or cannot be opened");
        return;
    }

    int deleted = 0;
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;

        String name = entry.name();
        if (!entry.isDirectory()) {
            String fullPath = String("/logs/") + name;
            if (SD.remove(fullPath)) {
                ++deleted;
                LOG("Deleted file: " + fullPath);
            } else {
                LOG("Delete failed: " + fullPath);
            }
        }
        entry.close();
    }
    dir.close();

    String msg = "Logs cleared, deleted " + String(deleted) + " file(s)";
    LOG(msg);
    server.send(200, "text/plain", msg);
}



void startWiFiAndWeb() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, password);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    server.on("/", handleRoot);
    server.on("/exit", handleExit);
    server.on("/power-on", handlePowerOn);
    server.on("/power-off", handlePowerOff);
    server.on("/rtc-sync-browser", HTTP_POST, handleRTCSyncFromBrowser);
    server.on("/files", handleListFiles);
    server.on("/file", handleReadFile);
    server.on("/format-sd", handleFormatSDCard);
    server.on("/download", handleFileDownload);
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
    if (millis() - lastToggle > 500) {
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState ? HIGH : LOW);
        lastToggle = millis();
    }
}

void scanNetworks() {
    LOG("Scanning WiFi...");
    int n = WiFi.scanNetworks();
    if (n == 0) {
        LOG("No networks found.");
    } else {
        for (int i = 0; i < n; ++i) {
            String line = WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + "dBm)";
            if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) line += " [open]";
            LOG(line);
        }
    }
}
