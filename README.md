# GlowMate SmartLamp

基于 ESP32-WROOM-32 的智能台灯系统，集成多传感器、多模式灯光控制、离线语音识别、BLE 直连微信小程序，支持 OTA 固件升级。

## 特性

| 类别 | 能力 |
|------|------|
| **灯光** | 7 种模式（常规/冷光/暖光/氛围/夜灯/学习/自控），5 级亮度，14 色选择，50Hz 呼吸灯动画 |
| **控制** | 物理按键 + ASRPRO 离线语音（22 条中文命令）+ 微信小程序 BLE 直连 |
| **感知** | DHT11 温湿度 + 光敏电阻 ADC + LD2402 毫米波人体存在检测 |
| **显示** | 0.96" SSD1306 OLED 128x64（LVGL 9 渲染，WiFi/BLE/闹钟图标，大字时钟+秒） |
| **联网** | WiFi STA（SNTP 北京时间 + OTA 固件升级），BLE Peripheral（Nordic NUS） |
| **拓展** | 闹钟倒计时 + 学习计时，OTA 双分区回滚保护 |

## 硬件架构

```
┌──────────────────────────────────────────────────┐
│                  ESP32-WROOM-32                  │
│                                                  │
│  GPIO25 ─── WS2812B (32 LED, RMT)                │
│  GPIO21 ─── SSD1306 OLED SDA (I2C0, 0x3C)        │
│  GPIO22 ─── SSD1306 OLED SCL                     │
│  GPIO26 ─── DHT11 温湿度                         │
│  GPIO34 ─── 光敏电阻 (ADC1_CH6)                  │
│  GPIO13 ─── LD2402 雷达 UART1 RX (115200bps)     │
│  GPIO4  ─── LD2402 雷达 UART1 TX                 │
│  GPIO27 ─── LD2402 雷达 IO (有人/无人)            │
│  GPIO16 ─── ASRPRO 语音 UART2 RX (9600bps)       │
│  GPIO17 ─── ASRPRO 语音 UART2 TX                 │
│  GPIO18 ─── MODE 按键 (模式切换/长按闹钟)         │
│  GPIO19 ─── ADJUST 按键 (亮度/颜色调节)           │
└──────────────────────────────────────────────────┘
```

## 软件架构

```
app_main
 ├─ wifi_task (prio 10)    WiFi + SNTP + OTA
 ├─ ble_uart_task (prio 8) BLE Nordic NUS 透传 + 状态上报
 ├─ main_ctrl_task (prio 8) 核心业务逻辑 (模式/亮度/颜色/闹钟)
 ├─ voice_task (prio 8)    ASRPRO UART2 4字节帧协议
 ├─ ws2812b_task (prio 7)  WS2812B 刷新 + 呼吸灯动画
 ├─ sensor_task (prio 6)   DHT11 + ADC + 雷达 + 按键扫描
 ├─ oled_task (prio 5)     OLED LVGL 定时刷新
 └─ radar_task (prio 3)    LD2402 UART 行解析
```

**任务间通信**：

```
按键/语音/BLE ──→ g_cmd_queue ──→ main_ctrl_task ──→ led / oled / alarm
传感器 ──→ g_sensor_queue ──→ main_ctrl_task
main_ctrl ──→ g_ble_upload_queue ──→ ble_uart_task ──→ 手机
              g_system_events (EventGroup) ──→ 各任务同步
```

## 灯光模式

| ID | 模式 | 说明 |
|----|------|------|
| 0 | 常规灯 | 暖白 5 档亮度 |
| 1 | 冷光灯 | 蓝白 5 档亮度 |
| 2 | 暖光灯 | 暖黄 5 档亮度 |
| 3 | 氛围灯 | 14 色呼吸渐变（50Hz 动画，~24s 循环） |
| 4 | 夜灯 | 雷达 ≤200cm 触发，暖白最低档 |
| 5 | 学习灯 | 雷达 ≤120cm 触发，启动学习计时器 |
| 6 | 自控灯 | 7 色可选，亮度随环境光自适应 |

## 语音命令

ASRPRO 离线语音识别模块，22 条中文命令：

| 命令 | 操作 | 命令 | 操作 |
|------|------|------|------|
| 打开台灯 | 常规模式 亮度4 | 自控模式 | 自控灯-白 |
| 关闭台灯 | 常规模式 亮度0 | 自控灯-青/黄/紫/蓝/红/绿 | 自控灯各颜色 |
| 最大亮度 | 亮度4 | 亮度加/减 | 相对调节 |
| 冷光/暖光/彩光模式 | 模式切换 | 亮度1/2/3/4 | 绝对设置 |
| 夜灯/学习模式 | 模式切换 | | |

ESP32 可反向控制 ASRPRO 播报反馈（已达最亮/最暗/闹钟设置成功/闹钟铃响）。

## 微信小程序

BLE 直连（Nordic NUS 协议），无需云端中继：

- **连接页**：按 "SmartLamp" 名称扫描 BLE 设备
- **控制页**：模式切换、亮度滑块、颜色选择、温湿度显示、学习计时
- **协议**：下行 3 字节 `[mode][light][color]`，上行 6 字节状态

## 项目结构

```
Smart-Lamp/
├── main/                     # 主程序入口 + 头文件
│   ├── main.c                # app_main(), 7 任务创建
│   └── main.h                # 全局类型/事件位/句柄
├── components/               # 自定义组件（8 个）
│   ├── lamp_core/            # 核心业务逻辑（模式/亮度/颜色/闹钟）
│   ├── lamp_led/             # WS2812B RMT 驱动 + 颜��查表
│   ├── lamp_display/         # SSD1306 + LVGL 9 UI（v1.0.2）
│   ├── lamp_sensor/          # DHT11 + ADC + 按键 + 雷达入口
│   ├── lamp_radar/           # LD2402 UART 距离解析
│   ├── lamp_key/             # 按键状态机（非阻塞 50ms 扫描）
│   ├── lamp_voice/           # ASRPRO UART2 4字节帧协议
│   ├── lamp_ble/             # BLE NimBLE Nordic NUS 透传
│   ├── lamp_alarm/           # 闹钟/学习计时 FreeRTOS Timer
│   └── lamp_wifi/            # WiFi STA + SNTP + OTA
├── WxApp/                    # 微信小程序源码
├── managed_components/       # ESP-IDF 托管依赖
│   ├── espressif__led_strip/ # WS2812B RMT 驱动
│   ├── lvgl__lvgl/           # LVGL 9 图形库
│   └── espressif__esp_lvgl_port/ # LVGL ESP 移植层
├── partitions.csv            # 分区表（nvs + otadata + factory + ota_0/1）
├── sdkconfig.defaults         # ESP-IDF 默认配置
├── test/                     # BLE 测试用例
└── rules/                    # 开发规范文档
```

## 快速开始

### 环境要求

- **ESP-IDF** v5.x（项目使用 v5.4+）
- **ESP32-WROOM-32** 开发板
- **Python 3.8+**（ESP-IDF 工具链依赖）
- **微信开发者工具**（小程序调试）

### 编译 & 烧录

```bash
# 1. 激活 ESP-IDF 环境
. ~/esp/esp-idf/export.sh       # Linux/macOS
# 或 %IDF_PATH%\export.bat      # Windows

# 2. 配置 WiFi SSID/密码
idf.py menuconfig                # → GlowMate WiFi Configuration

# 3. 编译 + 烧录 + 串口监视
idf.py build flash monitor
```

### 首次使用

1. 上电后 OLED 显示 "SmartLamp" 启动屏
2. WiFi 自动连接（约 3-10 秒），连接后同步北京时间
3. 微信小程序搜索 "SmartLamp" 蓝牙设备并连接
4. 物理按键、语音命令、微信小程序三种方式均可控制

## OTA 固件升级

设备上电联网后自动检查服务器版本：

```
HTTP GET http://<server>/version.txt   → 对比本地 CONFIG_APP_PROJECT_VER
HTTP GET http://<server>/smartlamp.bin → esp_https_ota() → esp_restart()
```

支持双分区（ota_0 / ota_1）+ bootloader 回滚保护


## 版本历史

| 版本 | 日期 | 变更 |
|------|------|------|
| 1.0.2 | 2026-08 | OLED 升级 LVGL 9 渲染，图标化状态栏，四行紧排布局 |
| 1.0.1 | 2026-07 | BLE 替换巴法云，NimBLE Nordic NUS，OTA 固件升级 |
| 1.0.0 | 2026-06 | STM32+ESP8266 → ESP32 单芯片移植，FreeRTOS 6任务架构 |

## License

MIT
