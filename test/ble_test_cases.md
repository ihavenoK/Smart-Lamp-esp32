# SmartLamp BLE 蓝牙通信测试用例

> 固件版本: ESP-IDF v6.0.2 / NimBLE NUS  
> 测试工具: Python bleak + nRF Connect + 微信小程序  
> 协议: Nordic UART Service (NUS)  
> 设备名称: `SmartLamp`  
> 更新日期: 2026-08-01

---

## 协议摘要

| 层 | UUID | 方向 | 格式 |
|---|---|---|---|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | — | NUS |
| RX Char | `6E400002-...` | 手机→台灯 (Write) | 3 bytes: `[mode][light][color]` |
| TX Char | `6E400003-...` | 台灯→手机 (Notify) | 6 bytes: `[mode][light][color][temp][humi][study]` |

| 字段 | 范围 | 含义 |
|---|---|---|
| mode | 0-6 | 灯光模式 |
| light | 0-4 | 亮度档位 |
| color | 0-6 | 颜色编号 (0=白 1=青 2=黄 3=紫 4=蓝 5=红 6=绿) |
| temp | 0-50 | 温度 ℃ |
| humi | 20-90 | 湿度 % |
| study | 0-255 | 学习计时 分钟 |

---

## 功能测试

### TC-01 设备扫描

| 项 | 内容 |
|---|---|
| 前置条件 | ESP32 上电, NimBLE 广播已启动 |
| 步骤 | 1. 打开 nRF Connect / 微信小程序 / `ble_test.py --scan-only` |
| | 2. 等待 5 秒 |
| 预期结果 | 发现设备名 `SmartLamp`, RSSI ≥ -90 dBm |
| 自动化 | `ble_test.py` → `test_scan()` |

### TC-02 建立连接

| 项 | 内容 |
|---|---|
| 前置条件 | TC-01 通过 |
| 步骤 | 1. 点击设备建立连接 |
| | 2. 观察 ESP32 串口输出 `connected, handle=xx` |
| 预期结果 | 连接成功, MTU=512 |
| 自动化 | `ble_test.py` → `test_connect()` |

### TC-03 服务/特征发现

| 项 | 内容 |
|---|---|
| 前置条件 | TC-02 通过 |
| 步骤 | 1. 获取 GATT Service 列表 |
| | 2. 在 NUS Service 下查找 RX / TX 特征 |
| 预期结果 | 找到 `6E400001` 服务, 含 `6E400002` (Write) + `6E400003` (Notify) |
| 自动化 | `ble_test.py` → `test_services()` |

### TC-04 订阅 Notify

| 项 | 内容 |
|---|---|
| 前置条件 | TC-03 通过 |
| 步骤 | 1. 对 TX Characteristic 启用 CCCD Notify |
| 预期结果 | `write_ccc_descriptor` 返回成功, 后续每 2 秒收到 6 字节状态帧 |
| 自动化 | `ble_test.py` → `test_notify_subscribe()` |

### TC-05 常规灯模式

| 项 | 内容 |
|---|---|
| 步骤 | 写入 `[00 04 00]` → mode=0 常规, light=4 最亮, color=0 |
| 预期结果 | WS2812B 全部点亮暖白色 (EE,EE,00), Notify 回 `[00 04 00 ...]` |
| 自动化 | `ble_test.py` → `test_write_mode(0,4,0)` |

### TC-06 冷光灯模式

| 项 | 内容 |
|---|---|
| 步骤 | 写入 `[01 03 00]` → mode=1 冷光, light=3 |
| 预期结果 | WS2812B 蓝白色 (99,99,DD), Notify mode=1 |
| 自动化 | `ble_test.py` → `test_write_mode(1,3,0)` |

### TC-07 暖光灯模式

| 项 | 内容 |
|---|---|
| 步骤 | 写入 `[02 02 00]` → mode=2 暖光, light=2 |
| 预期结果 | WS2812B 琥珀色 (AA,AA,88), Notify mode=2 |
| 自动化 | `ble_test.py` → `test_write_mode(2,2,0)` |

### TC-08 氛围灯模式

| 项 | 内容 |
|---|---|
| 步骤 | 写入 `[03 00 00]` → mode=3 氛围 |
| 预期结果 | WS2812B 进入呼吸循环 (绿→红→蓝→黄→青→品→白), Notify mode=3 |
| 自动化 | `ble_test.py` → `test_write_mode(3,0,0)` |

### TC-09 夜灯模式

| 项 | 内容 |
|---|---|
| 步骤 | 写入 `[04 00 00]` → mode=4 夜灯 |
| 预期结果 | 有红外触发→亮 1 档暖白; 无人→灭灯, Notify mode=4 |
| 自动化 | `ble_test.py` → `test_write_mode(4,0,0)` |

### TC-10 学习灯模式

| 项 | 内容 |
|---|---|
| 步骤 | 写入 `[05 00 00]` → mode=5 学习 |
| 预期结果 | 同夜灯红外逻辑 + study_time 字段递增, Notify study_time > 0 |
| 自动化 | `ble_test.py` → `test_write_mode(5,0,0)` |

### TC-11 自控灯模式

| 项 | 内容 |
|---|---|
| 步骤 | 写入 `[06 00 02]` → mode=6 自控, color=2 黄色 |
| 预期结果 | WS2812B 黄色, 亮度随环境光 ADC 变化, Notify mode=6 |
| 自动化 | `ble_test.py` → `test_write_mode(6,0,2)` |

### TC-12 正常断开

| 项 | 内容 |
|---|---|
| 步骤 | 调用 `closeBLEConnection` / `ble.disconnect()` |
| 预期结果 | ESP32 输出 `disconnected`, 自动恢复广播 |
| 自动化 | `ble_test.py` → `test_disconnect()` |

### TC-13 异常断开 (手机超出范围)

| 项 | 内容 |
|---|---|
| 步骤 | 物理遮挡 ESP32 / 走远 > 10 米触发超时断开 |
| 预期结果 | ESP32 30 秒内检测到断开, 自动恢复广播; 手机端回调触发 |

---

## 边界测试

### TC-B1 参数越界

| 项 | 内容 |
|---|---|
| 步骤 | 依次写入 `[07 00 00]` `[00 05 00]` `[00 00 14]` `[FF FF FF]` |
| 预期结果 | 固件 clamp 到有效范围或直接拒绝; 不崩溃, 不卡死 |
| 自动化 | `ble_test.py` → `test_boundary_write()` |

### TC-B2 快速连续写入

| 项 | 内容 |
|---|---|
| 步骤 | 以 50ms 间隔连续写入 5 次 `[00 0x 00]` (x=0..4) |
| 预期结果 | 灯带按序变化, 不丢帧; 写入完成后 Notify 正常推送 |
| 自动化 | `ble_test.py` → `test_concurrent_write()` |

### TC-B3 断连重连

| 项 | 内容 |
|---|---|
| 步骤 | 断开 → 等待 1s → 重新连接 → 发送 `[00 04 00]` |
| 预期结果 | 重连成功, 灯带点亮; 广播在断开 1s 内恢复 |
| 自动化 | `ble_test.py` → `test_reconnect()` |

### TC-B4 写入 1 字节 / 2 字节

| 项 | 内容 |
|---|---|
| 步骤 | 写入 `[00]` (1 字节) 和 `[00 04]` (2 字节) |
| 预期结果 | 固件返回 `BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN`, 不崩溃 |

### TC-B5 连续订阅/取消订阅 5 次

| 项 | 内容 |
|---|---|
| 步骤 | start_notify → stop_notify → start_notify (循环 5 次) |
| 预期结果 | 每次 start_notify 后正常收到数据, stop 后停止推送 |

---

## Notify 数据校验

| 字段 | 校验规则 |
|---|---|
| mode | 0-6, 与最后一次写入一致 |
| light | 0-4, 与最后一次写入一致 |
| color | 0-6 (自控/氛围模式) |
| temp | 0-50, 值与 DHT11 读数 ±2℃ |
| humi | 20-90, 值与 DHT11 读数 ±5% |
| study_time | 学习模式下 > 0, 其他模式 = 0 |

---

## 自动化运行

```bash
# 全部测试
python test/ble_test.py

# 仅扫描
python test/ble_test.py --scan-only

# 指定设备
python test/ble_test.py --device "AA:BB:CC:DD:EE:FF"
```

输出示例:

```
============================================================
  SmartLamp BLE 通信测试套件
  协议: Nordic UART Service (NUS)
============================================================
[21:36:01.123] TC-01: 扫描 BLE 设备...
[21:36:03.456]   发现: name=SmartLamp addr=AA:BB:CC RSSI=-52dBm
  ✓ PASS: 发现 SmartLamp (AA:BB:CC)
[21:36:03.458] TC-02: 连接设备...
  ✓ PASS: 已连接 (MTU=512)
...
============================================================
  测试完成: 18 项, 通过 18, 失败 0
  结果: ✓ 全部通过
============================================================
```

---

## 手动测试清单 (微信小程序)

| # | 操作 | 预期 | 通过 |
|---|---|---|---|
| 1 | 打开小程序, 点击「搜索设备」 | 发现 SmartLamp, RSSI 信号条显示 | ☐ |
| 2 | 点击设备卡片 | 进入主控页, 顶部显示 Connected + 温湿度 | ☐ |
| 3 | 点击「常规」→ 拖动亮度到 4 | 台灯亮暖白最亮, 页面显示 Brightness 4/4 | ☐ |
| 4 | 点击「冷光」→ 亮度 2 | 台灯蓝白, 页面显示 Brightness 2/4 | ☐ |
| 5 | 点击「氛围」 | 台灯开始呼吸渐变, 亮度/颜色控件隐藏 | ☐ |
| 6 | 点击「夜灯」 | 遮挡红外传感器→灯亮, 移开→灯灭 | ☐ |
| 7 | 点击「学习」 | 学习计时数字开始递增 | ☐ |
| 8 | 点击「自控」→ 选黄色 | 台灯黄色, 亮度随环境变化, 无亮度滑块 | ☐ |
| 9 | 物理断开 (按 RESET) | 页面显示 Disconnected, 点 Reconnect 回到扫描页 | ☐ |
| 10 | 扫描页点断开 | 回到扫描页, 重新搜到 SmartLamp | ☐ |
