# ESP32-C3 Battery Monitor & Scheduled Power Controller

[中文版 / Chinese version](README_zh.md)

An ESP32-C3 Mini based system that monitors a 4S LiFePO4 battery, logs temperature/humidity,
controls a scheduled power latch, and exposes a WiFi web panel for status, file browsing and
RTC sync. Data is stored on an SD card. The device has been in continuous field use and is
maintained as a long-running, reliability-focused build.

## Features

- **Battery monitoring** of a 4S LiFePO4 pack via a resistor divider + ESP32 ADC (factory eFuse calibrated).
- **State-of-charge (SOC)** estimation using a LiFePO4 resting-voltage lookup table with interpolation.
- **Low-power protection** with hysteresis: cuts the load at a low threshold and only restores power
  once the battery has recovered to a clearly higher threshold, preventing on/off oscillation.
- **Scheduled power on/off** at configured hours, with a manual override that lasts until end of day.
- **Temperature/humidity sampling** (SHT35) logged to daily CSV files.
- **SD card logging** of sensor data, voltage history, and a timestamped system log.
- **WiFi AP + web panel** (long-press the button) for live status, SD file browsing/download,
  log clearing, and RTC sync from the browser/phone.

## Hardware

| Component        | Detail                                              |
|------------------|-----------------------------------------------------|
| MCU              | ESP32-C3 Mini                                       |
| Display          | SSD1306 128x32 OLED (I2C)                            |
| RTC              | DS3231 (I2C)                                         |
| Sensor           | SHT35 temperature/humidity (I2C, address 0x44)      |
| Storage          | microSD card (SPI)                                   |
| Battery          | 4S LiFePO4 ("12V" pack, 12.8V nominal, 14.6V full)  |
| Power control    | Latch/relay on `POWER_LATCH_PIN`                    |

### Pin assignments

| Function          | GPIO |
|-------------------|------|
| I2C SDA           | 21   |
| I2C SCL           | 20   |
| Button            | 9    |
| LED               | 8    |
| Power latch       | 10   |
| Battery ADC       | 0 (ADC1_CH0) |
| SD CS             | 4    |
| SD MISO           | 1    |
| SD MOSI           | 3    |
| SD SCK            | 2    |

### Battery voltage divider

The ESP32 ADC cannot read 12V+ directly (input max ~3.3V), so a resistor divider scales the
battery voltage down:

```
Battery + (12-14.6V) --[ R1 33k ]--+--[ R2 10k ]-- Battery - (GND)
                                   |
                                   +------> ESP32 GPIO0 (ADC1_CH0)

ESP32 GND ------------------------------------------ Battery -   (common ground required)
```

- Divider ratio = (R1 + R2) / R2 = (33k + 10k) / 10k = **4.3** (`VOLTAGE_DIVIDER_RATIO` in `BatteryMonitor.cpp`).
- Battery voltage = (measured pin millivolts / 1000) * 4.3.
- **Headroom note:** at full charge (14.6V) the divider tap is ~3.40V, slightly above the 3.3V ADC
  limit, so readings near 100% are compressed. This is acceptable for this build; if precise
  high-end readings or extra pin headroom are needed, increase the ratio (e.g. 33k/6.8k ~= 5.85).

## Build & flash (arduino-cli)

```bash
# Install arduino-cli (into ~/bin)
mkdir -p ~/bin
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=~/bin sh
export PATH="$HOME/bin:$PATH"

# ESP32 board core
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32

# Libraries (ArduinoJson must be 7.x)
arduino-cli lib install "ArduinoJson@^7.0.0"
arduino-cli lib install "RTClib"
arduino-cli lib install "Adafruit SHT31 Library"

# Compile
arduino-cli compile --fqbn esp32:esp32:esp32c3 .

# Flash (replace the port from `arduino-cli board list`)
arduino-cli upload --fqbn esp32:esp32:esp32c3 -p /dev/ttyACM0 .
arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200
```

> Requires arduino-esp32 core 2.0+ (for `analogReadMilliVolts`) and ArduinoJson 7.x.
> `SD`, `Wire`, `WiFi`, `WebServer` ship with the ESP32 core.

## Usage

### Normal operation
On boot the device initializes all modules, logs an initial voltage reading, then in the main loop:
- samples temperature/humidity every minute,
- logs battery voltage every 10 minutes,
- enforces the low-power cutoff/recovery logic,
- applies the scheduled power on/off.

### WiFi mode
Long-press the button (3s) to start a WiFi access point and web server:
- SSID: `ESP32-AP`, password: `12345678`, URL: `http://192.168.4.1`
- The LED blinks while WiFi mode is active.
- Use the "Exit WiFi mode" button on the page to return to normal operation.

### Web endpoints

| Path                | Method | Description                                  |
|---------------------|--------|----------------------------------------------|
| `/`                 | GET    | Status panel (temp, humidity, battery, time) |
| `/files`            | GET    | JSON file tree of the SD card                |
| `/file?path=`       | GET    | Stream a file's contents (restricted to `/logs/`) |
| `/download?path=`   | GET    | Download a file (restricted to `/logs/`)     |
| `/power-on`         | GET    | Manually power on the latch                  |
| `/power-off`        | GET    | Manually power off the latch                 |
| `/format-sd`        | GET    | Delete all files under `/logs/`              |
| `/rtc-sync-browser` | POST   | Sync the RTC from a JSON `{ "time": ISO }`   |
| `/exit`             | GET    | Request exit from WiFi mode                  |

File read/download are restricted to the `/logs/` directory (path whitelist).

## Battery logic

Defined in the main sketch:

| Constant                  | Value | Meaning                                                |
|---------------------------|-------|--------------------------------------------------------|
| `LOW_POWER_THRESHOLD`     | 14    | Cut the load at ~14% SOC (~12.5V resting)              |
| `RECOVERY_PERCENT`        | 40    | Restore the load once resting SOC reaches ~40% (~13.1V)|
| `POWER_ON_HOUR`           | 6     | Scheduled power-on hour                                |
| `POWER_OFF_HOUR`          | 20    | Scheduled power-off hour                               |

The gap between cutoff (14%) and recovery (40%) is the hysteresis band (~0.6V) that prevents
rapid on/off cycling: after recovery, load sag will not immediately drop back below the cutoff.
During shutdown the external load is disconnected, so the measured voltage is the resting voltage
(accurate and stable). Tune `RECOVERY_PERCENT` higher to be safer against oscillation, or lower to
resume sooner.

## Data formats

- `/logs/<YYYY-MM-DD>.csv` -- temperature/humidity: `date,time,temp,humidity`
- `/logs/voltage.csv` -- battery: `date,time,voltage,percent`
- `/logs/systemLog_<YYYY-MM-DD>.txt` -- timestamped system log lines

## Project structure

| File                    | Responsibility                                  |
|-------------------------|-------------------------------------------------|
| `*.ino`                 | Main loop: scheduling, sampling, power logic    |
| `BatteryMonitor.*`      | ADC reading + LiFePO4 SOC estimation            |
| `PowerLatch.*`          | Power latch/relay control                       |
| `ButtonHandler.*`       | Debounced short/long press detection            |
| `SensorManager.*`       | SHT35 temperature/humidity                       |
| `RTC.*`                 | DS3231 RTC access                                |
| `StorageManager.*`      | SD card init, CSV/log writing, `LOG` macro       |
| `WebServerHandler.*`    | WiFi AP + HTTP endpoints                         |
| `WebUI.*`               | Status panel HTML                                |

## Calibration tips

- Verify the divider ratio by measuring battery voltage and the divider tap voltage simultaneously;
  set `VOLTAGE_DIVIDER_RATIO = V_battery / V_tap` to absorb resistor tolerance.
- The LiFePO4 discharge curve is very flat, so voltage-based SOC is imprecise in the 20-90% range;
  only the top and bottom "knees" change voltage meaningfully. Tune the SOC table endpoints against
  measured full/empty values.

### ADC high-end compression (this build)

The physical divider is 33k/10k = 4.33, but near full charge the tap voltage (~3.1V) exceeds the
ESP32-C3 ADC's reliable linear range and the ADC reads low. Measured at full charge: true battery
13.42V, true tap 3.097V, but the ADC reports only 2.912V (~0.19V low). To compensate without
changing hardware, `VOLTAGE_DIVIDER_RATIO` is set to an empirically calibrated **4.61**
(= 13.42 / 2.912), anchoring the full-charge end.

Caveats and alternatives:
- This single-point calibration may read slightly high at the low end (the ADC error is largest at
  the top), so the cutoff may trigger a little later than the nominal threshold. Acceptable for a
  LiFePO4 pack with a BMS.
- For full-range accuracy, take a second measurement near empty (~12.5-12.8V) and build a two-point
  calibration.
- The clean hardware fix is to lower the divider so the tap stays within ~2.5V: e.g. change the
  lower resistor 10k -> 6.8k (ratio ~5.85, full-charge tap ~2.3V), then no software calibration is
  needed and the reading is linear across the whole range.
