#!/usr/bin/env python3
"""
SmartLamp BLE 通信测试脚本
============================
基于 bleak 库，测试 ESP32 SmartLamp 的 Nordic UART Service (NUS)
完整覆盖：扫描 → 连接 → 服务发现 → 写入控制 → 接收状态 → 断开

协议:
  Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
  RX (Write): 6E400002-...   → 3 bytes [mode][light][color]
  TX (Notify): 6E400003-...  ← 6 bytes [mode][light][color][temp][humi][study]

用法:
  python ble_test.py                      # 运行全部测试
  python ble_test.py --scan-only          # 仅扫描
  python ble_test.py --device "xx:xx:xx"  # 指定设备地址
"""

import asyncio
import sys
import struct
import argparse
from datetime import datetime

try:
    from bleak import BleakScanner, BleakClient
except ImportError:
    print("[ERROR] bleak not installed. Run: pip install bleak")
    sys.exit(1)

# ====== 协议常量 ======

DEVICE_NAME    = "SmartLamp"
SERVICE_UUID   = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
RX_CHAR_UUID   = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  # Write (phone → lamp)
TX_CHAR_UUID   = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  # Notify (lamp → phone)

# 灯光模式
MODES = {
    0: "NORMAL  (常规/暖白)",
    1: "COLD    (冷光/蓝白)",
    2: "WARM    (暖光/琥珀)",
    3: "COLOR   (氛围/呼吸)",
    4: "NIGHT   (夜灯/红外)",
    5: "STUDY   (学习/计时)",
    6: "AUTO    (自控/光敏)",
}

# 测试结果
passed = 0
failed = 0
notify_data = []


# ====== 工具函数 ======

def log(msg):
    """时间戳日志"""
    ts = datetime.now().strftime("%H:%M:%S.%f")[:12]
    print(f"[{ts}] {msg}")


def ok(msg):
    global passed
    passed += 1
    print(f"  ✓ PASS: {msg}")


def fail(msg):
    global failed
    failed += 1
    print(f"  ✗ FAIL: {msg}")


# ====== Notify 回调 ======

def notify_handler(sender, data):
    """接收 TX Notify 数据: 6 bytes"""
    notify_data.append(bytes(data))
    if len(data) >= 6:
        mode, light, color, temp, humi, study = struct.unpack("BBBBBB", data[:6])
        log(f"  ← Notify: mode={mode}({MODES.get(mode,'?')}) "
            f"light={light} color={color} "
            f"temp={temp}°C humi={humi}% study={study}min")


# ====== 测试用例 ======

async def test_scan():
    """TC-01: 扫描 SmartLamp 设备"""
    log("TC-01: 扫描 BLE 设备...")
    devices = await BleakScanner.discover(timeout=5.0, return_adv=True)

    found = []
    for addr, (dev, adv) in devices.items():
        name = dev.name or adv.local_name or ""
        if name == DEVICE_NAME or (adv.service_uuids and SERVICE_UUID.lower() in adv.service_uuids):
            found.append((addr, dev, adv))

    if not found:
        fail("未发现 SmartLamp 设备")
        return None

    addr, dev, adv = found[0]
    log(f"  发现: name={dev.name} addr={addr} RSSI={adv.rssi}dBm")
    ok(f"发现 SmartLamp ({addr})")
    return addr


async def test_connect(client):
    """TC-02: 建立 BLE 连接"""
    log("TC-02: 连接设备...")
    try:
        await client.connect(timeout=10.0)
        ok(f"已连接 (MTU={client.mtu_size})")
        return True
    except Exception as e:
        fail(f"连接失败: {e}")
        return False


async def test_services(client):
    """TC-03: 服务 / 特征发现"""
    log("TC-03: 发现 GATT 服务...")

    svc = None
    for s in client.services:
        if s.uuid.upper() == SERVICE_UUID:
            svc = s
            break

    if not svc:
        fail("未找到 NUS 服务")
        return None, None

    rx_char = None
    tx_char = None
    for c in svc.characteristics:
        uuid = c.uuid.upper()
        if uuid == RX_CHAR_UUID:
            rx_char = c
        elif uuid == TX_CHAR_UUID:
            tx_char = c

    if not rx_char:
        fail("未找到 RX (Write) 特征")
        return None, None
    if not tx_char:
        fail("未找到 TX (Notify) 特征")
        return None, None

    ok(f"NUS 服务 OK | RX={rx_char.uuid[:20]}... TX={tx_char.uuid[:20]}...")
    return rx_char, tx_char


async def test_notify_subscribe(client, tx_char):
    """TC-04: 订阅 TX Notify"""
    log("TC-04: 订阅 TX Notify...")
    try:
        await client.start_notify(tx_char, notify_handler)
        ok("Notify 订阅成功")
        return True
    except Exception as e:
        fail(f"Notify 订阅失败: {e}")
        return False


async def test_write_mode(client, rx_char, mode, light, color, desc):
    """TC-05~11: 写入模式切换命令"""
    log(f"TC-{5+mode:02d}: 写入 {desc}")
    payload = struct.pack("BBB", mode, light, color)
    log(f"  发送: [{mode:02X} {light:02X} {color:02X}]  ({desc})")
    try:
        await client.write_gatt_char(rx_char, payload, response=True)
        ok(f"{desc} 写入成功")
        return True
    except Exception as e:
        fail(f"{desc} 写入失败: {e}")
        return False


async def test_notify_received(timeout=3.0):
    """验证收到 Notify 数据"""
    global notify_data
    notify_data.clear()
    log(f"  等待 Notify 数据 (最多 {timeout}s)...")
    await asyncio.sleep(timeout)
    if notify_data:
        ok(f"收到 {len(notify_data)} 帧 Notify 数据")
        return list(notify_data)
    else:
        fail("未收到 Notify 数据")
        return []


async def test_disconnect(client):
    """TC-12: 断开连接"""
    log("TC-12: 断开连接...")
    try:
        await client.disconnect()
        ok("正常断开")
        return True
    except Exception as e:
        fail(f"断开异常: {e}")
        return False


# ====== 边界测试 ======

async def test_boundary_write(client, rx_char):
    """边界测试: 参数边界"""
    log("TC-B1: 边界测试 — 参数范围")

    tests = [
        # (mode, light, color, expect_ok)
        (0, 0, 0, True,   "最小值"),
        (6, 4, 6, True,   "最大值"),
        (7, 0, 0, False,  "超范围 mode=7"),  # 固件会 clamp 但不应崩溃
        (0, 5, 0, False,  "超范围 light=5"),
        (0, 0, 14, False, "超范围 color=14"),
        (0xff, 0xff, 0xff, False, "全 0xFF"),
    ]

    for mode, light, color, expect, desc in tests:
        payload = struct.pack("BBB", mode, light, color)
        try:
            await client.write_gatt_char(rx_char, payload, response=True)
            if expect:
                ok(f"  边界 {desc}: 写入成功 (预期)")
            else:
                fail(f"  边界 {desc}: 应拒绝但写入成功")
        except Exception:
            if expect:
                fail(f"  边界 {desc}: 应成功但失败")
            else:
                ok(f"  边界 {desc}: 被拒绝 (预期)")


async def test_concurrent_write(client, rx_char):
    """并发测试: 快速连续写入 5 次"""
    log("TC-B2: 并发测试 — 快速 5 次写入")
    for i in range(5):
        payload = struct.pack("BBB", 0, i % 5, 0)
        await client.write_gatt_char(rx_char, payload, response=False)
        await asyncio.sleep(0.05)
    await asyncio.sleep(2.0)
    # 验证固件未崩溃 (能继续收 Notify 即正常)
    if notify_data:
        ok(f"  并发写入后仍可收到 Notify (帧数: {len(notify_data)})")
    else:
        fail("  并发写入后无 Notify, 固件可能异常")


async def test_reconnect(client, addr):
    """重连测试"""
    log("TC-B3: 重连测试")
    await client.disconnect()
    await asyncio.sleep(1.0)
    await client.connect(timeout=10.0)
    if client.is_connected:
        ok("  重连成功")
    else:
        fail("  重连失败")


# ====== 主流程 ======

async def main():
    global passed, failed

    parser = argparse.ArgumentParser(description="SmartLamp BLE Test Suite")
    parser.add_argument("--scan-only", action="store_true", help="仅扫描设备")
    parser.add_argument("--device", type=str, help="指定设备 MAC 地址")
    args = parser.parse_args()

    print("=" * 60)
    print("  SmartLamp BLE 通信测试套件")
    print("  协议: Nordic UART Service (NUS)")
    print("=" * 60)

    # TC-01: 扫描
    addr = args.device or await test_scan()
    if not addr:
        log("→ 无设备, 退出")
        sys.exit(1)

    if args.scan_only:
        log("→ 仅扫描模式, 完成")
        sys.exit(0)

    # TC-02: 连接
    async with BleakClient(addr) as client:
        if not await test_connect(client):
            return

        # TC-03: 服务发现
        rx_char, tx_char = await test_services(client)
        if not rx_char:
            return

        # TC-04: 订阅 Notify
        rx_obj = client.services.get_characteristic(tx_char.uuid) if hasattr(tx_char, 'uuid') else None
        if not rx_obj:
            # fallback: use client's start_notify with uuid
            await test_notify_subscribe(client, tx_char.uuid)
        else:
            await test_notify_subscribe(client, rx_obj)

        # TC-05~11: 模式切换
        mode_seq = [
            (0, 4, 0, "常规灯 亮度4"),
            (1, 3, 0, "冷光灯 亮度3"),
            (2, 2, 0, "暖光灯 亮度2"),
            (3, 0, 0, "氛围灯"),
            (4, 0, 0, "夜灯"),
            (5, 0, 0, "学习灯"),
            (6, 0, 2, "自控灯 黄色"),
        ]
        for mode, light, color, desc in mode_seq:
            await test_write_mode(client, rx_char.uuid, mode, light, color, desc)
            await asyncio.sleep(1.0)  # 等 Notify

        await test_notify_received(timeout=2.0)

        # TC-B1: 边界
        await test_boundary_write(client, rx_char.uuid)

        # TC-B2: 并发
        await test_concurrent_write(client, rx_char.uuid)

        # TC-B3: 重连
        await test_reconnect(client, addr)

        # TC-12: 断开
        await test_disconnect(client)

    # ====== 总结 ======
    total = passed + failed
    print()
    print("=" * 60)
    print(f"  测试完成: {total} 项, 通过 {passed}, 失败 {failed}")
    if failed == 0:
        print("  结果: ✓ 全部通过")
    else:
        print(f"  结果: ✗ {failed} 项失败")
    print("=" * 60)


if __name__ == "__main__":
    asyncio.run(main())
