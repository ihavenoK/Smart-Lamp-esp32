#ifndef RADAR_H
#define RADAR_H

/**
 * @file radar.h
 * @brief HLK-LD2402 24GHz 毫米波人体存在传感器 UART 驱动
 *
 * 根据用户手册 V1.08:
 *   - 正常工作模式 ASCII 输出: "OFF\r\n" 或 "distance: XXX\r\n"
 *   - 刷新周期 165ms, 波特率 115200-8N1
 *   - 出厂默认配置即插即用, GPIO IO 引脚 (J2Pin2) 为有人/无人指示
 */

#include <stdint.h>

/* UART1: LD2402 (J2Pin4/TX → ESP32 RX, J2Pin5/RX → ESP32 TX) */
#define RADAR_UART_NUM      UART_NUM_1
#define RADAR_UART_BAUD     115200U
#define RADAR_GPIO_TX       4U
#define RADAR_GPIO_RX       13U

/* 检测状态 */
#define RADAR_STATE_NONE    0U
#define RADAR_STATE_MOVING  1U
#define RADAR_STATE_STATIC  2U

void radar_init(void);
void radar_task_init(void);
uint16_t radar_get_distance(void);
uint8_t  radar_get_presence(void);

#endif /* RADAR_H */
