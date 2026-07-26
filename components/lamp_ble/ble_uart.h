#ifndef BLE_UART_H
#define BLE_UART_H

#include <stdint.h>

/**
 * @file ble_uart.h
 * @brief BLE UART 透传 (Nordic NUS 标准协议)
 *
 * 替代原 ESP8266 串口中转方案，手机通过 BLE 串口 APP 直接控制台灯。
 *
 * NUS UUID (Nordic 标准):
 *   Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
 *   TX Char: 6E400002 (Notify, 设备→手机, 主动上报状态)
 *   RX Char: 6E400003 (Write,  手机→设备, 接收控制命令)
 *
 * 协议帧格式 (兼容原 STM32↔ESP8266 二进制协议):
 *   上行 (设备→手机, TX Notify): [mode][light][color][temp][humi][study]
 *      共 6 字节, 由 main_ctrl_task 状态变化时推送
 *   下行 (手机→设备, RX Write):  [mode][light][color]
 *      共 3 字节, BLE 收到后组 lamp_cmd_t 推入 g_cmd_queue
 *
 * 兼容 APP: Nordic Toolbox, LightBlue, Serial Bluetooth Terminal 等
 */

/**
 * @brief NimBLE 初始化 — 必须在 app_main 主线程同步调用
 *
 * 对齐官方 ble_uart_install(), 完成:
 *   nimble_port_init → ble_hs_cfg → 内置服务 → GATT 表注册 → store init
 *
 * 此函数必须在 FreeRTOS 调度器启动前 (app_main 内) 完成,
 * 不能放在 FreeRTOS 任务中, 否则 ble_gatts_count_cfg 会返回 BLE_HS_EINVAL。
 */
void ble_uart_init(void);

/**
 * @brief 启动 BLE Host 任务 (优先级 8)
 *
 * 调用 nimble_port_freertos_init 启动 NimBLE 事件循环,
 * on_sync 回调中自动开始广播。同时启动状态上报主循环。
 *
 * 前提: ble_uart_init() 已成功完成。
 */
void ble_uart_task_init(void);

#endif /* BLE_UART_H */
