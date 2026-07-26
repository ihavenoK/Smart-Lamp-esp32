#ifndef VOICE_H
#define VOICE_H

#include <stdint.h>

/**
 * @brief 语音组件 — ASRPRO UART2 4字节协议收发
 *
 * GPIO16=U2RXD (ESP32侧接收), GPIO17=U2TXD (ESP32侧发送)
 * Datasheet 确认: U2RXD=GPIO16, U2TXD=GPIO17 (硬件默认)
 *
 * 协议 (与原版完全一致):
 *   收: 0xFF, code(1-22), 0xFE, 0xEE  -> 语音命令
 *   发: 0xFF, event, 0xFE, 0xEE        -> 语音播报反馈
 *
 * 22条语音命令通过查表法映射到 lamp_cmd_t
 */

/* 语音播报事件码 */
#define VOICE_EVT_BRIGHT_MAX    0x01U   /* 已达最大亮度 */
#define VOICE_EVT_BRIGHT_MIN    0x02U   /* 已达最小亮度 */
#define VOICE_EVT_ALARM_SET     0x03U   /* 闹钟设置成功 */
#define VOICE_EVT_ALARM_RING    0x04U   /* 闹钟时间到 */

/* UART 参数 */
#define VOICE_UART_NUM      UART_NUM_2
#define VOICE_UART_BAUD     9600U  /* ASRPRO 规格书默认 9600bps */
#define VOICE_GPIO_TX       17U
#define VOICE_GPIO_RX       16U

/**
 * @brief 初始化 ASRPRO UART2
 */
void voice_init(void);

/**
 * @brief 语音接收任务 (UART事件驱动, 优先级 8)
 * 解析4字节帧, 查表映射为 lamp_cmd_t -> g_cmd_queue
 */
void voice_task_init(void);

/**
 * @brief 发送语音播报指令给 ASRPRO
 * @param event 事件码 (VOICE_EVT_*)
 */
void voice_send_event(uint8_t event);

#endif /* VOICE_H */
