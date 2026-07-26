#ifndef ALARM_H
#define ALARM_H

#include <stdint.h>

/**
 * @brief 闹钟组件 — FreeRTOS 软件定时器替代 STM32 RTC 闹钟
 *
 * 原 STM32: MyRTC.c — RTC 硬件闹钟 + LSE 时钟源
 * ESP32:    FreeRTOS Timer + SNTP 网络时间
 *
 * 学习计时: FreeRTOS Timer 替代原 STM32 TIM4 中断
 */

/**
 * @brief 初始化闹钟
 */
void alarm_init(void);

/**
 * @brief 设置闹钟 (一次性定时器)
 * @param seconds 倒计时秒数
 */
void alarm_start(uint32_t seconds);

/**
 * @brief 取消闹钟
 */
void alarm_stop(void);

/**
 * @brief 获取闹钟剩余时间 (秒)
 */
uint32_t alarm_get_remaining(void);

/**
 * @brief 闹钟是否正在计时
 * @return 1=计时中, 0=未计时
 */
uint8_t alarm_is_running(void);

/**
 * @brief 启动学习计时器 (替代 TIM4 1秒中断)
 * 每 60 秒递增 g_study_time
 */
void study_timer_start(void);

/**
 * @brief 停止学习计时器
 */
void study_timer_stop(void);

#endif /* ALARM_H */
