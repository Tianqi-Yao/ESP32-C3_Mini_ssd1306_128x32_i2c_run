# ESP32-C3 电池监测与定时供电控制器

[English version / 英文版](README.md)

基于 ESP32-C3 Mini 的系统：监测 4S LiFePO4 电池、采集温湿度、控制定时供电锁存，
并提供 WiFi 网页面板用于查看状态、浏览文件和校准 RTC。数据存储在 SD 卡上。
本设备已长期实地运行，以稳定可靠为目标进行维护。

## 功能

- **电池监测**：通过电阻分压 + ESP32 ADC（eFuse 出厂校准）测量 4S LiFePO4 电池。
- **电量估算(SOC)**：用 LiFePO4 静置电压查表 + 线性插值。
- **低电保护（带迟滞）**：低于断电阈值切断负载，必须充到明显更高的恢复阈值才恢复供电，杜绝来回开关。
- **定时开关机**：在设定的小时开/关；手动操作可覆盖到当天结束。
- **温湿度采集**：SHT35，按天写入 CSV。
- **SD 卡日志**：传感器数据、电压历史、带时间戳的系统日志。
- **WiFi AP + 网页面板**（长按按钮进入）：实时状态、SD 文件浏览/下载、清空日志、用浏览器/手机时间校准 RTC。

## 硬件

| 组件      | 说明                                              |
|-----------|---------------------------------------------------|
| 主控      | ESP32-C3 Mini                                     |
| 显示屏    | SSD1306 128x32 OLED（I2C）                         |
| RTC       | DS3231（I2C）                                      |
| 传感器    | SHT35 温湿度（I2C，地址 0x44）                     |
| 存储      | microSD 卡（SPI）                                  |
| 电池      | 4S LiFePO4（“12V”电池组，标称 12.8V，满充 14.6V）  |
| 供电控制  | `POWER_LATCH_PIN` 上的锁存/继电器                  |

### 引脚分配

| 功能        | GPIO |
|-------------|------|
| I2C SDA     | 21   |
| I2C SCL     | 20   |
| 按钮        | 9    |
| LED         | 8    |
| 供电锁存    | 10   |
| 电池 ADC    | 0（ADC1_CH0） |
| SD CS       | 4    |
| SD MISO     | 1    |
| SD MOSI     | 3    |
| SD SCK      | 2    |

### 电池分压

ESP32 的 ADC 不能直接测 12V 以上（输入上限约 3.3V），所以用电阻分压把电池电压缩小：

```
电池+ (12-14.6V) --[ R1 33k ]--+--[ R2 10k ]-- 电池- (GND)
                               |
                               +------> ESP32 GPIO0 (ADC1_CH0)

ESP32 GND --------------------------------------- 电池-   （必须共地）
```

- 分压比 = (R1 + R2) / R2 = (33k + 10k) / 10k = **4.3**（`BatteryMonitor.cpp` 中的 `VOLTAGE_DIVIDER_RATIO`）。
- 电池电压 = (管脚毫伏 / 1000) * 4.3。
- **余量提醒**：满充 14.6V 时分压抽头约 3.40V，略超 3.3V 的 ADC 上限，所以满电附近（接近 100%）读数会被压缩。
  对本设备可接受；若需要顶端读数更准或更多引脚余量，可加大比值（如 33k/6.8k ≈ 5.85）。

## 编译与烧录（arduino-cli）

```bash
# 安装 arduino-cli（装到 ~/bin）
mkdir -p ~/bin
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=~/bin sh
export PATH="$HOME/bin:$PATH"

# ESP32 开发板核心
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32

# 依赖库（ArduinoJson 必须是 7.x）
arduino-cli lib install "ArduinoJson@^7.0.0"
arduino-cli lib install "RTClib"
arduino-cli lib install "Adafruit SHT31 Library"

# 编译
arduino-cli compile --fqbn esp32:esp32:esp32c3 .

# 烧录（端口用 `arduino-cli board list` 查到的替换）
arduino-cli upload --fqbn esp32:esp32:esp32c3 -p /dev/ttyACM0 .
arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200
```

> 需要 arduino-esp32 核心 2.0+（提供 `analogReadMilliVolts`）和 ArduinoJson 7.x。
> `SD`、`Wire`、`WiFi`、`WebServer` 随 ESP32 核心自带。

## 使用

### 正常运行
开机初始化各模块、记录一次初始电压，然后主循环里：
- 每分钟采集温湿度，
- 每 10 分钟记录电压，
- 执行低电断电/恢复逻辑，
- 执行定时开关机。

### WiFi 模式
长按按钮（3 秒）启动 WiFi 热点和网页服务器：
- SSID：`ESP32-AP`，密码：`12345678`，地址：`http://192.168.4.1`
- WiFi 模式下 LED 闪烁。
- 点页面上的 “Exit WiFi mode” 返回正常运行。

### 网页接口

| 路径                | 方法   | 说明                                       |
|---------------------|--------|--------------------------------------------|
| `/`                 | GET    | 状态面板（温度、湿度、电池、时间）         |
| `/files`            | GET    | SD 卡文件树（JSON）                        |
| `/file?path=`       | GET    | 流式输出文件内容（限定 `/logs/`）          |
| `/download?path=`   | GET    | 下载文件（限定 `/logs/`）                  |
| `/power-on`         | GET    | 手动开启供电锁存                           |
| `/power-off`        | GET    | 手动关闭供电锁存                           |
| `/format-sd`        | GET    | 删除 `/logs/` 下所有文件                   |
| `/rtc-sync-browser` | POST   | 用 JSON `{ "time": ISO }` 校准 RTC         |
| `/exit`             | GET    | 请求退出 WiFi 模式                         |

文件读取/下载被限制在 `/logs/` 目录内（路径白名单）。

## 电池逻辑

在主程序中定义：

| 常量                  | 值  | 含义                                          |
|-----------------------|-----|-----------------------------------------------|
| `LOW_POWER_THRESHOLD` | 14  | 约 14% SOC（静置约 12.5V）切断负载            |
| `RECOVERY_PERCENT`    | 40  | 静置电量充到约 40%（约 13.1V）恢复供电        |
| `POWER_ON_HOUR`       | 6   | 定时开机小时                                  |
| `POWER_OFF_HOUR`      | 20  | 定时关机小时                                  |

断电阈值(14%)与恢复阈值(40%)之间的差就是迟滞带（约 0.6V），用于防止快速来回开关：
恢复供电后，负载下垂不会立刻又跌破断电线。断电期间外部负载已断开，所以读到的是静置电压
（准确且稳定）。把 `RECOVERY_PERCENT` 调高更不易抖动，调低则恢复更快。

## 可靠性

设备需要长期无人值守运行，因此加了两道防护应对卡死和时钟失效：

### 看门狗
在 `setup()` 末尾启用任务看门狗（`esp_task_wdt`），超时 30 秒，并在每轮 `loop()` 最顶部喂狗。
一旦 loop 卡死（SD/I2C/WiFi 锁死），看门狗会触发复位、设备自动重启，而不是一直僵死等人工断电。
超时时间宽于最长的合法阻塞调用（`WiFi.scanNetworks()`，数秒）。初始化通过 `ESP_ARDUINO_VERSION_MAJOR`
同时兼容 arduino-esp32 核心 2.x 和 3.x。

### RTC 失效降级
所有与时间相关的逻辑都依赖 DS3231 RTC，所以用 `isRtcValid()`（RTC 成功初始化且年份在 2024-2099）
对 RTC 缺失/损坏做防御性处理：
- RTC 无效时**暂停**定时开关机和每日标志清零，绝不在垃圾时间上切换电源；并记一条节流告警（最多每分钟一次）。
- 日志文件名和时间戳兜底为 `unknown-date.csv` / `systemLog_unknown.txt` 和归零时间戳，
  避免把乱码日期文件散落到卡上。
- 基于 `millis()` 的采样、电压记录和低电保护**不受影响**，照常运行（它们不依赖 RTC）。
- 开机时三个初始化（`rtcInit` / `sensorInit` / `storageInit`）失败会醒目地记录日志，不再静默。

## 数据格式

- `/logs/<YYYY-MM-DD>.csv` —— 温湿度：`date,time,temp,humidity`
- `/logs/voltage_<YYYY-MM>.csv` —— 电池（按月滚动）：`date,time,voltage,percent`
- `/logs/systemLog_<YYYY-MM-DD>.txt` —— 带时间戳的系统日志

RTC 时间无效时，带日期的文件名会兜底为 `unknown-date.csv`、`voltage_unknown.csv`、`systemLog_unknown.txt`。

## 项目结构

| 文件                    | 职责                                       |
|-------------------------|--------------------------------------------|
| `*.ino`                 | 主循环：定时、采集、电源逻辑               |
| `BatteryMonitor.*`      | ADC 读取 + LiFePO4 电量估算               |
| `PowerLatch.*`          | 供电锁存/继电器控制                        |
| `ButtonHandler.*`       | 防抖的短按/长按检测                        |
| `SensorManager.*`       | SHT35 温湿度                               |
| `RTC.*`                 | DS3231 RTC 访问                            |
| `StorageManager.*`      | SD 卡初始化、CSV/日志写入、`LOG` 宏        |
| `WebServerHandler.*`    | WiFi AP + HTTP 接口                        |
| `WebUI.*`               | 状态面板 HTML                              |

## 校准建议

- 校准分压比：同时量电池电压和分压抽头电压，令 `VOLTAGE_DIVIDER_RATIO = V电池 / V抽头`，可吸收电阻误差。
- LiFePO4 放电曲线极平，靠电压估 SOC 在 20%-90% 区间不准，只有两端“拐点”电压变化才明显。
  请按实测的满/空电压微调 SOC 表两端的值。

### ADC 顶端压缩（本设备实测）

物理分压比是 33k/10k = 4.33，但接近满充时抽头电压（约 3.1V）超出 ESP32-C3 ADC 的可靠线性区，
ADC 会读低。满充实测：电池真实 13.42V、抽头真实 3.097V，但 ADC 只认 2.912V（偏低约 0.19V）。
为在不动硬件的前提下补偿，`VOLTAGE_DIVIDER_RATIO` 取实测校准值 **4.61**（= 13.42 / 2.912），以满充端为锚。

注意与替代方案：
- 该单点校准在低电端可能略偏高（ADC 误差在顶端最大），所以断电会比设定阈值稍晚触发。
  对带 BMS 的 LiFePO4 电池可接受。
- 若要全区间精确，在快没电时（约 12.5-12.8V）再补测一个点，做两点校准。
- 彻底的硬件解法是减小分压、让抽头落在约 2.5V 以内：例如把下臂 10k 换成 6.8k（比值约 5.85，
  满充抽头约 2.3V），这样无需任何软件校准，全区间线性准确。
