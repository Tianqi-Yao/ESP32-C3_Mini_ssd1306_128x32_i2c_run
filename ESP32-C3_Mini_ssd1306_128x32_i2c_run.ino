#include <Wire.h>
#include "SensorManager.h"
#include "BatteryMonitor.h"
#include "StorageManager.h"
#include "ButtonHandler.h"
#include "PowerLatch.h"
#include "RTC.h"
#include "WebServerHandler.h"
#include <SD.h>
#include "esp_task_wdt.h"

#define WDT_TIMEOUT_S       30   // Watchdog timeout (s); wider than the longest blocking op (WiFi scan)

// ========== Pin & timing configuration ==========
#define SDA_PIN             21
#define SCL_PIN             20
#define BTN_PIN             9
#define LED_PIN             8
#define POWER_LATCH_PIN     10

#define TEMP_INTERVAL_MS        1 * 60000UL         // Sample temperature/humidity every 1 min
#define VOLTAGE_INTERVAL_MS     10 * 60000UL        // Log battery voltage every 10 min
#define RECOVERY_CHECK_INTERVAL 1 * 60000UL         // Check resting-voltage recovery every 1 min

#define POWER_ON_HOUR           5
#define POWER_OFF_HOUR          21
// Cutoff/recovery use hysteresis: the recovery threshold must be clearly higher than the
// cutoff threshold, so that after power is restored the load sag will not immediately drop
// back below the cutoff line -> prevents on/off oscillation.
#define LOW_POWER_THRESHOLD     14   // 4S LiFePO4: cut power at ~14% SOC (~12.5V resting) to protect the battery
#define RECOVERY_PERCENT        50   // Restore power once resting SOC reaches ~50% (~13.13V), leaving hysteresis margin

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
    // Rotate the voltage log monthly so the file stays bounded (~4300 lines/month) instead of
    // growing without limit (it used to be a single file appended every 10 min forever).
    String path;
    if (isRtcValid()) {
        DateTime d = getCurrentDateTime();
        char buf[40];
        snprintf(buf, sizeof(buf), "/logs/voltage_%04d-%02d.csv", d.year(), d.month());
        path = String(buf);
    } else {
        path = "/logs/voltage_unknown.csv";
    }
    File file = SD.open(path, FILE_APPEND);
    if (!file) {
        LOG("Failed to open voltage log file");
        return false;
    }

    char line[64];
    if (isRtcValid()) {
        DateTime now = getCurrentDateTime();
        snprintf(line, sizeof(line), "%04d-%02d-%02d,%02d:%02d:%02d,%.2f,%d",
            now.year(), now.month(), now.day(),
            now.hour(), now.minute(), now.second(),
            voltage, percent);
    } else {
        snprintf(line, sizeof(line), "0000-00-00,00:00:00,%.2f,%d", voltage, percent);
    }
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
    // Capture init results so failures are no longer silent (behavior still degrades gracefully).
    if (!rtcInit())     LOG("FATAL: RTC init failed");
    if (!sensorInit())  LOG("FATAL: sensor init failed");
    if (!storageInit()) LOG("FATAL: SD storage init failed");
    initPowerLatch(POWER_LATCH_PIN);

    LOG("All modules initialized");
    waitForValidBatteryRead();

    float voltage = readBatteryVoltage();
    int percent = getBatteryPercentage(voltage);
    logVoltageStatus(voltage, percent);
    LOG("Initial voltage logged at boot: " + String(voltage, 2) + "V (" + String(percent) + "%)");

    // Enable the task watchdog last, after the blocking init sequence above.
    // If the loop ever hangs (SD/I2C/WiFi lockup) the device auto-resets instead of freezing.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_task_wdt_config_t wdtConfig = {
        .timeout_ms = WDT_TIMEOUT_S * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    if (esp_task_wdt_init(&wdtConfig) == ESP_ERR_INVALID_STATE) {
        esp_task_wdt_reconfigure(&wdtConfig);  // already initialized by the core; just reconfigure
    }
#else
    esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
    esp_task_wdt_add(NULL);  // subscribe the loop task
    LOG("Watchdog enabled (" + String(WDT_TIMEOUT_S) + "s)");
}

// ========== Main loop ==========
void loop() {
    esp_task_wdt_reset();  // feed the watchdog first thing every iteration

    unsigned long now = millis();
    bool rtcOk = isRtcValid();
    DateTime rtcNow = getCurrentDateTime();
    int hour = rtcNow.hour();

    // Time-dependent logic only runs when the RTC time is valid, so a dead/missing RTC
    // never toggles power on garbage time. millis()-based sampling below is unaffected.
    if (rtcOk) {
        // Clear the daily power on/off flags on day rollover (by date change, not a time window)
        int today = rtcNow.day();
        if (today != lastResetDay) {
            lastResetDay = today;
            powerOnTriggeredToday = false;
            powerOffTriggeredToday = false;
        }
    } else {
        // Throttled warning so an invalid RTC does not flood the log every loop
        static unsigned long lastRtcWarn = 0;
        if (lastRtcWarn == 0 || now - lastRtcWarn >= 60000UL) {
            lastRtcWarn = now;
            LOG("WARNING: RTC invalid; scheduling/day-rollover paused, using fallback timestamps");
        }
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
    //    The recovery threshold (50%) is well above the cutoff (14%), so load sag after recovery
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
    //    Gated on rtcOk: never act on an invalid RTC time.
    if (rtcOk && !lowPowerShutdown) {
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
