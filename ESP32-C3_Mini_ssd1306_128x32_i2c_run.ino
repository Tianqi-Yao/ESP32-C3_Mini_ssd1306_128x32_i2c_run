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
#define RECOVERY_CHECK_INTERVAL 1 * 60000UL         // 每1分钟检测静置电压是否恢复

#define POWER_ON_HOUR           6
#define POWER_OFF_HOUR          20
// 断电/恢复采用迟滞(hysteresis)：恢复阈值必须明显高于断电阈值，
// 这样恢复供电后即使负载拉低电压，也不会立刻又跌破断电线 → 杜绝来回开关。
#define LOW_POWER_THRESHOLD     14   // 4S LiFePO4：约14% SOC(静置约12.5V)即断电，保守护电池
#define RECOVERY_PERCENT        40   // 静置电量充到约40%(4S约13.1V)即恢复供电，留足迟滞缓冲

// ========== 运行状态 ==========
unsigned long lastTempLog = 0;
unsigned long lastVoltLog = 0;
unsigned long lastRecoveryCheck = 0;
bool lowPowerShutdown = false;
bool isWiFiMode = false;

bool powerOnTriggeredToday = false;
bool powerOffTriggeredToday = false;
int lastResetDay = -1;   // 上次清零开关机标志时的日期，用于跨天检测

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
    int percent = getBatteryPercentage(voltage);
    logVoltageStatus(voltage, percent);
    LOG("📈 开机初始电压记录完毕: " + String(voltage, 2) + "V (" + String(percent) + "%)");
}

// ========== 主循环 ==========
void loop() {
    unsigned long now = millis();
    DateTime rtcNow = getCurrentDateTime();

    int hour = rtcNow.hour();

    // 🌙 跨天清除开关机标志（按日期变化判定，不依赖瞬间时间窗口）
    int today = rtcNow.day();
    if (today != lastResetDay) {
        lastResetDay = today;
        powerOnTriggeredToday = false;
        powerOffTriggeredToday = false;
    }

    // 1️⃣ 每分钟采集温湿度
    if (now - lastTempLog >= TEMP_INTERVAL_MS || lastTempLog == 0) {
        lastTempLog = now;
        if (refreshSensorData()) {
            float temp = getTemperature();
            float hum = getHumidity();
            saveTemperatureHumidityWithTime(temp, hum);
        }
    }

    // 2️⃣ 每10分钟记录电压
    if (now - lastVoltLog >= VOLTAGE_INTERVAL_MS || lastVoltLog == 0) {
        lastVoltLog = now;
        float voltage = readBatteryVoltage();
        int percent = getBatteryPercentage(voltage);
        logVoltageStatus(voltage, percent);

        // 仅在尚未处于低电关机时才触发断电，避免关机期间重复触发、打乱恢复计时
        if (!lowPowerShutdown && percent <= LOW_POWER_THRESHOLD) {
            LOG("🛑 电量低(" + String(percent) + "%)，立即断电");
            powerOff();
            lowPowerShutdown = true;
            lastRecoveryCheck = now;
            return;
        }
    }

    // 3️⃣ 低电关机后：等电池(静置电压)充到恢复阈值就立刻恢复供电
    //    关机时外部负载已断开，此处读到的即静置电压，最准最稳，无需先通电试探。
    //    恢复阈值(40%)明显高于断电阈值(14%)，恢复后负载拉低电压也不会马上又断 → 不再来回开关。
    if (lowPowerShutdown && now - lastRecoveryCheck >= RECOVERY_CHECK_INTERVAL) {
        lastRecoveryCheck = now;
        int currentPercent = getBatteryPercentage();
        if (currentPercent >= RECOVERY_PERCENT) {
            LOG("✅ 电量恢复到 " + String(currentPercent) + "%，恢复供电并继续采集");
            powerOn();
            lowPowerShutdown = false;
        }
    }

    // 4️⃣ 定时开关机（每天仅触发一次，手动操作可覆盖至当天结束）
    // 用「已到点且当天未触发」判定，而非卡 second<5 的瞬间窗口，
    // 避免被同轮 loop 的 WiFi 扫描/SD 写入等阻塞操作跨过而错过。
    if (!lowPowerShutdown) {
        if (hour >= POWER_ON_HOUR && hour < POWER_OFF_HOUR && !powerOnTriggeredToday) {
            LOG("⏰ 定时开机触发");
            powerOn();
            powerOnTriggeredToday = true;
        }
        if (hour >= POWER_OFF_HOUR && !powerOffTriggeredToday) {
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
