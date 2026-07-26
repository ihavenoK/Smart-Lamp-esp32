#ifndef LAMP_CORE_H
#define LAMP_CORE_H

#include <stdint.h>
#include "main.h"          /* alarm_set_state_t, CMD_TYPE_*, LAMP_MODE_* */

/**
 * @brief 灯光核心控制 — 模式切换、亮度/颜色调整、闹钟逻辑
 *
 * 对应原 STM32 main.c 中 while(1) 的业务逻辑部分，
 * 改为事件驱动 + FreeRTOS 任务模式。
 */

/* ====== 全局状态变量 (原 STM32 main.c 全局变量) ====== */
extern uint8_t  g_mode;          /* 当前模式 0-6 */
extern uint8_t  g_light_level;   /* 当前亮度 0-4 */
extern uint8_t  g_color_index;   /* 当前颜色 0-13 */
extern uint8_t  g_temp;          /* 温度 */
extern uint8_t  g_humi;          /* 湿度 */
extern uint16_t g_study_time;    /* 学习时长(分钟) */
extern uint8_t  g_ir_state;      /* LD2402 IO引脚 (0=无人,1=有人, 备用) */
extern uint16_t g_radar_dist;    /* LD2402 UART 距离 (cm), 0=无人 */
extern uint8_t  g_radar_presence; /* LD2402 检测状态 0=无人 1=运动 2=静止 */
extern uint16_t g_adc_val;       /* 光敏ADC (0-4095) */

extern uint8_t  g_hour, g_min, g_sec;  /* 闹钟设定值 */
extern uint32_t g_alarm_time;          /* 闹钟总时长(秒) */
extern uint8_t  g_flag_count;          /* 是否在计时 */

/* 闹钟设置状态机 (多组件引用: key.c, display.c) */
extern alarm_set_state_t g_alarm_state;
extern uint8_t g_alarm_hour, g_alarm_min, g_alarm_sec;

/**
 * @brief 创建主控制任务
 *
 * 优先级 8, 栈 4096, 事件驱动
 * 接收 cmd_queue (按键/语音/云端统一命令),
 * 维护全局状态, 通知 ws2812b_task 和 oled_task 更新
 */
void main_ctrl_task_init(void);

/**
 * @brief 灯光模式设置 (对应原 Ligth_Set)
 *
 * @param mode  模式 0-6
 * @param level 亮度 0-4
 * @param color 颜色 0-13
 */
void lamp_mode_set(uint8_t mode, uint8_t level, uint8_t color);

#endif /* LAMP_CORE_H */
