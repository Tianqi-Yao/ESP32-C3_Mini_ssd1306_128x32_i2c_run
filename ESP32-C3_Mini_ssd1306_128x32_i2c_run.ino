#include <Wire.h>
#include "SensorManager.h"
#include "BatteryMonitor.h"
#include "StorageManager.h"
#include "ButtonHandler.h"
#include "PowerLatch.h"
#include "RTC.h"
#include "WebServerHandler.h"
#include <SD.h>

// ========== ⏱️ 定时参数 ==========
#define SDA_PIN             21
#define SCL_PIN             20
#define BTN_PIN             9
#define LED_PIN             8
#define POWER_LATCH_PIN     10

#define TEMP_INTERVAL_MS        1 * 60000UL         // 每1分钟采集温湿度
#define VOLTAGE_INTERVAL_MS     10 * 60000UL        // 每10分钟记录电压
#define RECOVERY_CHECK_INTERVAL 1 * 60000UL         // 每1分钟检测电压恢复
#define POWER_RETRY_INTERVAL    3UL * 3600000UL     // 每3小时尝试上电一次

#define POWER_ON_HOUR           6
#define POWER_OFF_HOUR          20
#define LOW_POWER_THRESHOLD     0
#define RECOVERY_PERCENT        95

// ========== 运行状态 ==========
unsigned long lastTempLog = 0;
unsigned long lastVoltLog = 0;
unsigned long lastPowerRetry = 0;
unsigned long lastRecoveryCheck = 0;
bool lowPowerShutdown = false;
bool isWiFiMode = false;

bool powerOnTriggeredToday = false;
bool powerOffTriggeredToday = false;

// ========== 电压日志 ==========
bool logVoltageStatus(float voltage, int percent) {
    String path = "/logs/voltage.csv";
    File file = SD.open(path, FILE_APPEND);
    if (!file) {
        LOG("⚠️ 无法打开电压日志文件");
        return false;
    }

    DateTime now = getCurrentDateTime();
    char line[64];
    snprintf(line, sizeof(line), "%04d-%02d-%02d,%02d:%02d:%02d,%.2f,%d",
        now.year(), now.month(), now.day(),
        now.hour(), now.minute(), now.second(),
        voltage, percent);
    file.println(line);
    file.close();
    LOG("🔋 电压记录: " + String(line));
    return true;
}

void waitForValidBatteryRead(int maxRetries = 20, int delayMs = 250) {
    for (int i = 0; i < maxRetries; ++i) {
        float voltage = readBatteryVoltage();
        if (voltage > 1.0) {  // 电池电压大于1V基本可认为是有效
            LOG("🔋 检测到有效电压: " + String(voltage, 2) + "V");
            return;
        }
        delay(delayMs);
    }
    LOG("⚠️ 等待电压采集超时，电压仍异常");
}

// ========== 初始化 ==========
void setup() {
    Serial.begin(115200);
    delay(500);

    Wire.begin(SDA_PIN, SCL_PIN);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    buttonInit(BTN_PIN);
    rtcInit();
    sensorInit();
    storageInit();
    initPowerLatch(POWER_LATCH_PIN);

    LOG("✅ 所有模块初始化完成");
    waitForValidBatteryRead();

    float voltage = readBatteryVoltage();
    int percent = getBatteryPercentage();
    logVoltageStatus(voltage, percent);
    LOG("📈 开机初始电压记录完毕: " + String(voltage, 2) + "V (" + String(percent) + "%)");
}

// ========== 主循环 ==========
void loop() {
    unsigned long now = millis();
    DateTime rtcNow = getCurrentDateTime();

    int hour = rtcNow.hour();
    int minute = rtcNow.minute();
    int second = rtcNow.second();

    // 🌙 每天凌晨清除开关机标志
    if (hour == 0 && minute == 0 && second < 5) {
        powerOnTriggeredToday = false;
        powerOffTriggeredToday = false;
    }

    // 1️⃣ 每分钟采集温湿度
    if (now - lastTempLog >= TEMP_INTERVAL_MS || lastTempLog == 0) {
        LOG("🟢 进入温湿度采集...");
        lastTempLog = now;
        if (refreshSensorData()) {
            float temp = getTemperature();
            float hum = getHumidity();
            saveTemperatureHumidityWithTime(temp, hum);
        }
    }

    // 2️⃣ 每10分钟记录电压
    if (now - lastVoltLog >= VOLTAGE_INTERVAL_MS || lastVoltLog == 0) {
        LOG("🟢 进入记录电压数据...");
        lastVoltLog = now;
        float voltage = readBatteryVoltage();
        int percent = getBatteryPercentage();
        logVoltageStatus(voltage, percent);

        if (percent <= LOW_POWER_THRESHOLD) {
            LOG("🛑 电量为0%，立即断电");
            powerOff();
            lowPowerShutdown = true;
            lastPowerRetry = now;
            lastRecoveryCheck = now;
            return;
        }
    }

    // 3️⃣ 低电关机后尝试恢复
    if (lowPowerShutdown && now - lastRecoveryCheck >= RECOVERY_CHECK_INTERVAL) {
        LOG("🟢 进入低电状态恢复检查...");
        lastRecoveryCheck = now;
        unsigned long sinceLastRetry = now - lastPowerRetry;
        int currentPercent = getBatteryPercentage();

        if (sinceLastRetry >= POWER_RETRY_INTERVAL) {
            LOG("⚡️ 到时间，尝试通电...");
            powerOn();
            delay(3000);
            currentPercent = getBatteryPercentage();
            if (currentPercent <= LOW_POWER_THRESHOLD) {
                LOG("🔋 电量仍为0%，继续断电");
                powerOff();
            } else {
                LOG("✅ 电量恢复，退出低电状态");
                lowPowerShutdown = false;
            }
        } else if (currentPercent >= RECOVERY_PERCENT) {
            LOG("⚡️ 电量 " + String(currentPercent) + "%，提前恢复供电");
            powerOn();
            lowPowerShutdown = false;
        }
    }

    // 4️⃣ 定时开关机（仅触发一次）
    if (!lowPowerShutdown) {
        if (hour == POWER_ON_HOUR && minute == 0 && second < 5 && !powerOnTriggeredToday) {
            LOG("⏰ 定时开机触发");
            powerOn();
            powerOnTriggeredToday = true;
        }
        if (hour == POWER_OFF_HOUR && minute == 0 && second < 5 && !powerOffTriggeredToday) {
            LOG("⏰ 定时关机触发");
            powerOff();
            powerOffTriggeredToday = true;
        }
    }

    // 5️⃣ 长按按钮进入 WiFi 模式
    ButtonEvent event = checkButtonEvent();
    if (event == LONG_PRESS && !isWiFiMode) {
        LOG("📶 进入 WiFi 模式");
        isWiFiMode = true;
        digitalWrite(LED_PIN, LOW);
        scanNetworks();
        startWiFiAndWeb();
    }

    // 6️⃣ 处理网页请求与退出 WiFi 模式
    if (isWiFiMode) {
        blinkLED(LED_PIN);
        handleClientRequests();
        if (shouldExitWiFiMode()) {
            LOG("🔌 退出 WiFi 模式");
            stopWiFiAndWeb();
            isWiFiMode = false;
            digitalWrite(LED_PIN, HIGH);
        }
    }

    delay(100);  // 避免资源爆满 & 提升响应
}
