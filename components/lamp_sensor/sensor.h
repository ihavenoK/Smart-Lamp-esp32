#ifndef SENSOR_H
#define SENSOR_H

#include <stdint.h>

/**
 * @brief 传感器组件 — DHT11 + 光敏ADC + LD2402 毫米波 + 按键扫描
 *
 * 对应原 STM32: DHT11.c + AD.c + IrDA.c + (部分 Key.c)
 * 传感器任务 2秒周期 (优先级 6)
 * 采集数据通过 g_sensor_queue 发给主任务
 */

/* ADC 通道: GPIO34 = ADC1_CH6 (仅输入, 无上下拉) */
#define ADC_GPIO           34U
#define ADC_CHANNEL        ADC_CHANNEL_6
#define ADC_ATTEN          ADC_ATTEN_DB_12    /* Atten=3, 0-2450mV */

/* DHT11 引脚 */
#define DHT11_GPIO         26U

/* LD2402 毫米波人体存在传感器 (24GHz)
 * OUT: GPIO27 输入, 高电平=有人, 低电平=无人
 * TX (J2Pin4) → GPIO5 (UART1 RX): 115200bps, 距离数据
 * RX (J2Pin5) → GPIO4 (UART1 TX): 配置命令 (如需) */
#define IR_GPIO            27U

/**
 * @brief 初始化所有传感器
 * DHT11 (RMT驱动), ADC1 (eFuse校准), 红外GPIO, 按键
 */
void sensor_init(void);

/**
 * @brief 传感器采集任务 (2秒周期, 优先级 6)
 * DHT11 + ADC + 红外 + 按键扫描
 */
void sensor_task_init(void);

#endif /* SENSOR_H */
