#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include "main.h"

/**
 * @brief OLED 显示组件 — SSD1306 128x64 I2C 驱动
 *
 * GPIO21=SDA, GPIO22=SCL (I2C0 硬件)
 * 对应原 STM32 OLED.c (软件I2C) -> 改用 ESP-IDF 硬件I2C + ssd1306 组件
 *
 * 四行布局 (与原版一致):
 *   Line 1: "TXX:XX" + "AXX:XX:XX" (时钟/闹钟)
 *   Line 2: "Humi:XX"  + "Temp:XX"
 *   Line 3: "Mode:X"   + "Level:X"
 *   Line 4: "Color:X"  + "Stu:XXXX"
 */

/**
 * @brief 初始化 OLED
 * I2C0, 地址 0x3C (SSD1306)
 * 加入等待：若 SSD1306 组件不存在, 使用软件 I2C 回退
 */
void display_init(void);

/**
 * @brief OLED 显示刷新任务 (200ms 周期, 优先级 5)
 * 读取全局 oled_data_t 更新显示
 */
void oled_task_init(void);

/**
 * @brief 更新 OLED 显示缓存
 */
void display_update(const oled_data_t *data);

#endif /* DISPLAY_H */
