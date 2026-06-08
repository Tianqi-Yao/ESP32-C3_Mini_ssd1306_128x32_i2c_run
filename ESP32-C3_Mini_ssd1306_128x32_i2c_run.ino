#include <Wire.h>
#include "SensorManager.h"
#include "BatteryMonitor.h"
#include "StorageManager.h"
#include "ButtonHandler.h"
#include "PowerLatch.h"
#include "RTC.h"
#include "WebServerHandler.h"
#include <SD.h>

// ========== Pin & timing configuration ==========
#define SDA_PIN             21
#define SCL_PIN             20
#define BTN_PIN             9
#define LED_PIN             8
#define POWER_LATCH_PIN     10

#define TEMP_INTERVAL_MS        1 * 60000UL         // Sample temperature/humidity every 1 min
#define VOLTAGE_INTERVAL_MS     10 * 60000UL        // Log battery voltage every 10 min
#define RECOVERY_CHECK_INTERVAL 1 * 60000UL         // Check resting-voltage recovery every 1 min

#define POWER_ON_HOUR           6
#define POWER_OFF_HOUR          20
// Cutoff/recovery use hysteresis: the recovery threshold must be clearly higher than the
// cutoff threshold, so that after power is restored the load sag will not immediately drop
// back below the cutoff line -> prevents on/off oscillation.
#define LOW_POWER_THRESHOLD     14   // 4S LiFePO4: cut power at ~14% SOC (~12.5V resting) to protect the battery
#define RECOVERY_PERCENT        50   // Restore power once resting SOC reaches ~40% (~13.1V), leaving hysteresis margin

// ========== Runtime state ==========
unsigned long lastTempLog = 0;
unsigned long lastVoltLog = 0;
unsigned long lastRecoveryCheck = 0;
bool lowPowerShutdown = false;
bool isWiFiMode = false;

bool powerOnTriggeredToday = false;
bool powerOffTriggeredToday = false;
int lastResetDay = -1;   // Day-of-month at last flag reset, used for day-rollover detection

// ========== Voltage log ==========
bool logVoltageStatus(float voltage, int percent) {
    String path = "/logs/voltage.csv";
    File file = SD.open(path, FILE_APPEND);
    if (!file) {
        LOG("Failed to open voltage log file");
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
    LOG("Voltage logged: " + String(line));
    return true;
}

void waitForValidBatteryRead(int maxRetries = 20, int delayMs = 250) {
    for (int i = 0; i < maxRetries; ++i) {
        float voltage = readBatteryVoltage();
        if (voltage > 1.0) {  // A reading above 1V is considered a valid battery measurement
            LOG("Valid voltage detected: " + String(voltage, 2) + "V");
            return;
        }
        delay(delayMs);
    }
    LOG("Timed out waiting for a valid voltage reading");
}

// ========== Initialization ==========
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

    LOG("All modules initialized");
    waitForValidBatteryRead();

    float voltage = readBatteryVoltage();
    int percent = getBatteryPercentage(voltage);
    logVoltageStatus(voltage, percent);
    LOG("Initial voltage logged at boot: " + String(voltage, 2) + "V (" + String(percent) + "%)");
}

// ========== Main loop ==========
void loop() {
    unsigned long now = millis();
    DateTime rtcNow = getCurrentDateTime();

    int hour = rtcNow.hour();

    // Clear the daily power on/off flags on day rollover (by date change, not a time window)
    int today = rtcNow.day();
    if (today != lastResetDay) {
        lastResetDay = today;
        powerOnTriggeredToday = false;
        powerOffTriggeredToday = false;
    }

    // 1) Sample temperature/humidity every minute
    if (now - lastTempLog >= TEMP_INTERVAL_MS || lastTempLog == 0) {
        lastTempLog = now;
        if (refreshSensorData()) {
            float temp = getTemperature();
            float hum = getHumidity();
            saveTemperatureHumidityWithTime(temp, hum);
        }
    }

    // 2) Log battery voltage every 10 minutes
    if (now - lastVoltLog >= VOLTAGE_INTERVAL_MS || lastVoltLog == 0) {
        lastVoltLog = now;
        float voltage = readBatteryVoltage();
        int percent = getBatteryPercentage(voltage);
        logVoltageStatus(voltage, percent);

        // Only trigger cutoff when not already in low-power shutdown, to avoid
        // repeated triggering during shutdown and disrupting the recovery timer.
        if (!lowPowerShutdown && percent <= LOW_POWER_THRESHOLD) {
            LOG("Low battery (" + String(percent) + "%), cutting power now");
            powerOff();
            lowPowerShutdown = true;
            lastRecoveryCheck = now;
            return;
        }
    }

    // 3) After low-power shutdown: restore power as soon as the resting voltage reaches the recovery threshold.
    //    During shutdown the external load is disconnected, so the reading here is the resting voltage:
    //    the most accurate and stable measurement, with no need to power on first to probe.
    //    The recovery threshold (40%) is well above the cutoff (14%), so load sag after recovery
    //    will not immediately re-trigger the cutoff -> no more on/off oscillation.
    if (lowPowerShutdown && now - lastRecoveryCheck >= RECOVERY_CHECK_INTERVAL) {
        lastRecoveryCheck = now;
        int currentPercent = getBatteryPercentage();
        if (currentPercent >= RECOVERY_PERCENT) {
            LOG("Battery recovered to " + String(currentPercent) + "%, restoring power and resuming sampling");
            powerOn();
            lowPowerShutdown = false;
        }
    }

    // 4) Scheduled power on/off (triggers once per day; a manual action overrides it until end of day).
    //    Uses "past the hour and not yet triggered today" instead of a narrow second<5 window,
    //    so it cannot be missed when a blocking operation (WiFi scan / SD write) spans that instant.
    if (!lowPowerShutdown) {
        if (hour >= POWER_ON_HOUR && hour < POWER_OFF_HOUR && !powerOnTriggeredToday) {
            LOG("Scheduled power-on triggered");
            powerOn();
            powerOnTriggeredToday = true;
        }
        if (hour >= POWER_OFF_HOUR && !powerOffTriggeredToday) {
            LOG("Scheduled power-off triggered");
            powerOff();
            powerOffTriggeredToday = true;
        }
    }

    // 5) Long-press the button to enter WiFi mode
    ButtonEvent event = checkButtonEvent();
    if (event == LONG_PRESS && !isWiFiMode) {
        LOG("Entering WiFi mode");
        isWiFiMode = true;
        digitalWrite(LED_PIN, LOW);
        scanNetworks();
        startWiFiAndWeb();
    }

    // 6) Handle web requests and exit from WiFi mode
    if (isWiFiMode) {
        blinkLED(LED_PIN);
        handleClientRequests();
        if (shouldExitWiFiMode()) {
            LOG("Exiting WiFi mode");
            stopWiFiAndWeb();
            isWiFiMode = false;
            digitalWrite(LED_PIN, HIGH);
        }
    }

    delay(100);  // Avoid saturating resources and improve responsiveness
}
