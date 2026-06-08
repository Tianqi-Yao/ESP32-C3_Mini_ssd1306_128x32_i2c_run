#include "WebServerHandler.h"
#include "StorageManager.h"     // 👈 引入 LOG 宏
#include "SensorManager.h"
#include "BatteryMonitor.h"
#include "PowerLatch.h"
#include "StorageManager.h"
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

// 路径白名单：只允许访问 /logs/ 下的文件，禁止目录穿越
static bool isAllowedPath(const String& p) {
    return p.startsWith("/logs/") && p.indexOf("..") < 0;
}

void handleFileDownload() {
    if (!server.hasArg("path")) {
        server.send(400, "text/plain", "❌ 缺少 path 参数");
        return;
    }

    String path = server.arg("path");
    if (!isAllowedPath(path)) {
        server.send(403, "text/plain", "❌ 路径不允许");
        LOG("⛔ WebServer: 拒绝越权下载: " + path);
        return;
    }
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        server.send(404, "text/plain", "❌ 文件不存在或是目录");
        return;
    }

    server.streamFile(file, "application/octet-stream");
    file.close();
}


void listFilesRecursively(File dir, JsonArray arr, String path = "") {
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;

        JsonObject obj = arr.add<JsonObject>();
        String name = entry.name();
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
    JsonDocument doc;  // 7.x 弹性文档，按需增长
    JsonArray root = doc.to<JsonArray>();

    File rootDir = SD.open("/");
    if (!rootDir) {
        server.send(500, "text/plain", "❌ 无法打开 SD 根目录");
        LOG("❌ WebServer: 无法打开 SD 根目录");
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
        server.send(400, "text/plain", "❌ 缺少 path 参数");
        LOG("❌ WebServer: Missing 'path' parameter for file read.");
        return;
    }

    String path = server.arg("path");
    if (!isAllowedPath(path)) {
        server.send(403, "text/plain", "❌ 路径不允许");
        LOG("⛔ WebServer: 拒绝越权读取: " + path);
        return;
    }
    File file = SD.open(path);
    if (!file || file.isDirectory()) {
        server.send(404, "text/plain", "❌ 文件不存在或是文件夹");
        LOG("❌ WebServer: Invalid file or directory requested: " + path);
        return;
    }

    // 流式输出，避免大文件拼成 String 撑爆内存导致重启
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
    LOG("📴 Exit request received via WebUI.");
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
        server.send(400, "text/plain", "❌ 缺少时间数据");
        LOG("❌ RTC sync failed: missing body data.");
        return;
    }

    String body = server.arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        server.send(400, "text/plain", "❌ JSON解析失败");
        LOG("❌ RTC sync failed: JSON parse error.");
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
        server.send(200, "text/plain", "✅ RTC 已用手机时间校准");
        LOG("⏱️ RTC synchronized from browser.");
    } else {
        server.send(400, "text/plain", "❌ 时间格式错误");
        LOG("❌ RTC sync failed: invalid time format.");
    }
}

void handleFormatSDCard() {
    File dir = SD.open("/logs");
    if (!dir || !dir.isDirectory()) {
        server.send(500, "text/plain", "❌ 无法打开日志目录");
        LOG("❌ 清空 SD 卡失败：/logs 不存在或无法打开");
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
                LOG("🗑️ 已删除文件: " + fullPath);
            } else {
                LOG("⚠️ 删除失败: " + fullPath);
            }
        }
        entry.close();
    }
    dir.close();

    String msg = "✅ 已清空日志文件，共删除 " + String(deleted) + " 个文件";
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

    LOG("🌐 WiFi AP started at: http://192.168.4.1");
}

void stopWiFiAndWeb() {
    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    exitRequested = false;
    LOG("📴 WiFi and Web server stopped.");
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
    LOG("🔍 Scanning WiFi...");
    int n = WiFi.scanNetworks();
    if (n == 0) {
        LOG("❌ No networks found.");
    } else {
        for (int i = 0; i < n; ++i) {
            String line = "📶 " + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + "dBm)";
            if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) line += " [open]";
            LOG(line);
        }
    }
}
