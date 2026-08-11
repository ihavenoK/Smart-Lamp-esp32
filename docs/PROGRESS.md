# Smart-Lamp 项目进度快照

> 本文档为项目唯一事实源。只记录阶段成果，不记录环境修复/中间排错。
> 版本: 1.0.3 | 更新: 2026-08-11

## 当前状态

**功能完整，待实机验证低功耗模块**（烧录 + 5 分钟闲置测试 + 按键唤醒测试）。

## 模块清单

| 模块 | 状态 | 说明 |
|------|------|------|
| lamp_core | ✅ | 7 模式 + 闹钟设置状态机 + **闲置睡眠判定（pm_guard_ok）** |
| lamp_led | ✅ | WS2812B RMT 驱动，50Hz 呼吸动画 |
| lamp_display | ✅ | SSD1306 + LVGL 9，新增 display_suspend/resume |
| lamp_sensor | ✅ | DHT11 + 光敏 ADC + 雷达 + 按键入口 |
| lamp_radar | ✅ | LD2402 UART 解析 |
| lamp_key | ✅ | 状态机消抖，**改接 GPIO32/14（RTC GPIO，反接 3.3V）** |
| lamp_voice | ✅ | ASRPRO UART2 帧协议 |
| lamp_ble | ✅ | NimBLE NUS 透传 |
| lamp_alarm | ✅ | FreeRTOS Timer 闹钟/学习计时 |
| lamp_wifi | ✅ | STA + SNTP + OTA |
| lamp_pm | ✅ 编译通过 | **新增**：Light Sleep 入口 + EXT1 唤醒 + RTC 时间备份，待实测 |

## 引脚占用

| GPIO | 功能 | 备注 |
|------|------|------|
| 4 | LD2402 UART1 TX | |
| 13 | LD2402 UART1 RX | 雷达占用，**勿接按键** |
| 14 | ADJUST 按键 | RTC GPIO，反接 3.3V，按下=高 |
| 16/17 | ASRPRO UART2 | |
| 21/22 | OLED I2C0 | |
| 25 | WS2812B | |
| 26 | DHT11 | |
| 27 | LD2402 雷达 IO | 非 RTC GPIO，无唤醒能力 |
| 32 | MODE 按键 | RTC GPIO，反接 3.3V，按下=高 |
| 34 | 光敏 ADC1_CH6 | 仅输入 |

## 遗留 BUG / 风险

| 项 | 状态 | 说明 |
|----|------|------|
| 固件分区余量 | ⚠️ 1%（~13KB） | LVGL+NimBLE+OTA 占满 1.25MB，后续功能空间紧张 |
| BLE 睡眠期不可发现 | 已知限制 | Light Sleep 停止广播，按键唤醒后可重连 |
| RTC 时钟漂移 | 已知限制 | 内部 RC ±5%，睡 1h 误差 ~3min，SNTP 唤醒后校正 |
| GPIO13 曾冲突 | ✅ 已解决 | MODE 键初选 GPIO13 与雷达 RX 冲突，改用 GPIO32 |

## 版本历史

| 版本 | 日期 | 变更 |
|------|------|------|
| 1.0.3 | 2026-08-11 | 低功耗模式：lamp_pm 组件、EXT1 唤醒、RTC 时钟备份、按键改接 GPIO32/14 |
| 1.0.2 | 2026-08 | LVGL 9 UI 重做 |
| 1.0.1 | 2026-07 | BLE 替换巴法云 |
| 1.0.0 | 2026-06 | ESP32 单芯片移植 |
