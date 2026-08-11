# RFC-002: 低功耗模式 + 备份 RTC 时钟

> 版本: 1.0 | 日期: 2026-08-05 | 级别: L2（功能新增，新增组件）

---

## 1. 背景与目标

**现状**：固件 8 个 FreeRTOS 任务全时运行，无任何睡眠逻辑。用户长时间不用灯时，ESP32 仍以 Active 模式 ~50mA 持续消耗。

**目标**：
- 闲置 N 秒后自动进入 Light Sleep（~0.8mA），按键或雷达靠近时唤醒
- 唤醒后 OLED 时间不丢失（RTC 内存 + SNTP 校准混合策略）

**非目标**（本 RFC 不做）：
- Deep Sleep（唤醒需全量重初始化，改动过大）
- 电池供电场景（当前为 USB 供电）

---

## 2. 硬件资源映射

### 唤醒源

| GPIO | 用途 | 唤醒方式 | 备注 |
|------|------|---------|------|
| GPIO32 | MODE 按键 | GPIO 唤醒 (EXT1) | RTC GPIO, 按键反接 3.3V, 按下=高电平 |
| GPIO14 | ADJUST 按键 | GPIO 唤醒 (EXT1) | RTC GPIO, 按键反接 3.3V, 按下=高电平 |

> **2026-08-11 设计修正（重要）**：
> 原方案使用 GPIO18/19 作 EXT1 唤醒源，实测发现两个硬件约束：
> 1. ESP32（非 S2/S3）的 EXT1 仅支持 `ANY_HIGH` / `ALL_LOW` 两种模式，没有 `ANY_LOW`；
>    低电平有效按键（按下=GND）用 ANY_HIGH 会因松开时上拉=高而立即自唤醒。
> 2. GPIO18/19 **不是 RTC GPIO**（RTC GPIO 仅 GPIO0-15 与 GPIO32-39），
>    EXT0/EXT1 唤醒源必须落在 RTC GPIO 上。
>
> 决策：按键从 GPIO18/19 改接 **GPIO32/14**（RTC GPIO、非 Strapping），
> 电路反接为"按下=高电平"（按键另一端从 GND 改接 3.3V），
> 配合 `ESP_EXT1_WAKEUP_ANY_HIGH` 实现双键即时唤醒。
> 睡眠时内部下拉=低电平，不会误触发自唤醒。
> 注：最初选定 GPIO13 作 MODE 键，后发现 GPIO13 已被 LD2402 雷达 RX 占用，
> 改选 GPIO32 规避冲突。

### 资源占用（无新增 GPIO）

| 资源 | 类型 | 占用者 | 备注 |
|------|------|--------|------|
| RTC_GPIO | 唤醒 | lamp_pm | GPIO32/14 均为 RTC GPIO，支持 Light Sleep 唤醒 |
| RTC_SLOW_MEM | 数据保留 | lamp_pm | 8KB，存基准时间戳（仅 16B 占用） |

---

## 3. 状态机迁移

```
现有状态（仅模式切换）：
  NORMAL ←→ COLD ←→ WARM ←→ COLOR ←→ NIGHT ←→ STUDY ←→ AUTO

新增顶层睡眠状态：
                              ┌─────────────┐
                              │   ACTIVE     │ ← 当前所有模式在此
                              └──────┬──────┘
                      闲置超时         │        按键/雷达唤醒
                              ┌──────▼──────┐
                              │  PRE_SLEEP   │ → 关 WS2812B、关 OLED 背光
                              └──────┬──────┘
                                     │ 外设关完
                              ┌──────▼──────┐
                              │  LIGHT_SLEEP │ → esp_light_sleep_start()
                              └──────────────┘
```

**触发条件**：
- 无 BLE 连接 + 无按键事件 + 无语音事件 + 无闹钟运行 → 计时 N 秒后进入 PRE_SLEEP
- N 通过 Kconfig 配置（默认 300s = 5 分钟）

**守卫条件（以下任一为真则阻止睡眠）**：
- BLE 已连接（`EVT_BLE_CONNECTED` 有效）
- 闹钟正在运行
- 学习计时器正在运行
- OTA 进行中
- 雷达检测到人（`radar_get_presence()` 为真或距离 < 阈值）——防止有人在灯前静坐看书时灯突然熄灭

---

## 4. 内存预算

| 段 | 预算 | 已用 | 余量 | 新增 |
|----|------|------|------|------|
| Flash (app) | 1.25MB | ~1.0MB | ~250KB | +2KB |
| DRAM | 520KB | — | — | +100B (静态变量) |
| RTC_SLOW_MEM | 8KB | 0 | 8KB | +16B (时间戳结构体) |

> Light Sleep 期间 DRAM 全保留，无需额外预算。

---

## 5. 任务划分

### 新增组件：`lamp_pm`

```
components/lamp_pm/
  lamp_pm.h          — 公开 API
  lamp_pm.c          — Light Sleep 入口 + 唤醒源配置 + RTC 时间恢复
  CMakeLists.txt     — 依赖 freertos log esp_sleep driver/rtc_io
```

| 功能 | 说明 |
|------|------|
| `lamp_pm_init()` | 配置 GPIO 唤醒源（ext1）|
| `lamp_pm_try_sleep()` | 检查守卫条件 → 关外设 → esp_light_sleep_start() |
| `lamp_pm_restore_periph()` | 唤醒后复位 GPIO、重开 OLED/WS2812B |
| `lamp_pm_save_time()` | 进睡眠前写 RTC_SLOW_MEM |
| `lamp_pm_restore_time()` | 唤醒后读 RTC_SLOW_MEM + 唤醒间隔 → 设系统时间 |

### 现有组件改动

| 组件 | 改动 | 行数 |
|------|------|------|
| `main.c` | `app_main` 末尾调 `lamp_pm_init()` | +1 |
| `lamp_core.c` | `main_ctrl_task` 中加闲置计时器，超时调 `lamp_pm_try_sleep()`；唤醒后调 `lamp_pm_restore_periph()` | +25 |
| `lamp_display.c` | 暴露 `display_suspend()` / `display_resume()` | +10 |
| `lamp_led.c` | 暴露 `led_suspend()` / `led_resume()` | +8 |

### 任务表（无新增 FreeRTOS 任务）

睡眠管理逻辑嵌入 `main_ctrl_task`（已存在），不新增独立任务。理由：睡眠判断依赖现有 cmd_queue 和事件组，放同一任务中减少同步开销。

---

## 6. 配网方案

不涉及。WiFi/SNTP 链路已有（`lamp_wifi`），唤醒后异步调 SNTP 校正时间。

---

## 7. RTC 时钟备份策略（方案 C 混合）

```
进睡眠前：
  写 RTC_SLOW_MEM：{base_time: Unix时间戳, wake_delay_s: 0}

唤醒后：
  1. 读 RTC_SLOW_MEM 的 base_time
  2. esp_sleep_get_wakeup_cause() 得 wake_cause
  3. 若 GPIO 唤醒：acc_sleep_s = (当前 RTC tick - 入睡时 RTC tick) / RTC_SLOW_FREQ
  4. approx_time = base_time + acc_sleep_s
  5. settimeofday(&approx_time, NULL)   ← 立即有效
  6. 后台：若 WiFi 已连 → SNTP 校正  → settimeofday 再次调整
```

精度：内部 RC 振荡器约 ±5% 漂移。睡 1 小时最大误差约 3 分钟。SNTP 在 WiFi 重连后（通常 3-10s）校正到毫秒级。对于台灯场景（不要求秒级精度），近似时间足够。

---

## 8. 风险与降级策略

| 风险 | 概率 | 影响 | 降级策略 |
|------|------|------|---------|
| Light Sleep 后 RMT 外设异常 | 低 | WS2812B 花灯 | 唤醒后全灯灭 + 重新 init RMT（`led_strip_init` 可重入） |
| GPIO 唤醒源漏配 | 低 | 按键按不醒 | 加超时自动唤醒（RTC Timer 30min），防止死睡 |
| SNTP 校正失败 | 中 | 时间漂移 | 已有 RTC 近似时间兜底，不阻塞正常使用 |
| OLED 唤醒后 I2C 总线卡死 | 低 | 花屏 | `display_resume()` 中重初始化 I2C + LVGL 刷新 |
| 闹钟在睡眠中到点 | 中 | 闹钟不响 | 进睡眠前检查守卫条件：闹钟运行中 → 不睡 |

**已知限制（Light Sleep 物理约束，非代码缺陷）**：
- 睡眠期间 BLE 广播停止，其他设备无法扫描/连接 SmartLamp；需按键唤醒后才可连接。已在 Kconfig 帮助文本中说明。

---

## 9. 验证标准

1. 闲置 300s 后串口日志打印 "Entering light sleep..."
2. 按键按下后唤醒，串口打印复位原因 "Wakeup from GPIO"
3. 唤醒后 OLED 时间显示（近似值），WiFi 连上后 SNTP 校正到准确时间
4. 唤醒后 WS2812B 正常恢复模式（非花灯）
5. 有 BLE 连接时不进入睡眠
6. 闹钟运行时睡眠不被触发

---

## 10. Kconfig 新增项

```kconfig
menu "SmartLamp Power Management"
    config SMARTLAMP_PM_ENABLE
        bool "Enable auto sleep"
        default y
        help
            Automatically enter light sleep after idle timeout.

    config SMARTLAMP_PM_IDLE_TIMEOUT_SEC
        int "Idle timeout (seconds)"
        default 300
        range 30 3600
        help
            Enter light sleep after this many seconds of inactivity.
endmenu
```
