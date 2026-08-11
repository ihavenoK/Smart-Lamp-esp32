#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include "main.h"

/**
 * @brief OLED 显示组件 - SSD1306 128x64 I2C + LVGL 9 (v1.0.2)
 *
 * GPIO21=SDA, GPIO22=SCL (I2C0 硬件)
 * 底层: ESP-IDF esp_lcd 内置 SSD1306 面板驱动
 * 渲染: LVGL 9 (I1 单色全缓冲) + esp_lvgl_port
 *
 * UI 布局 (0.96" 128x64):
 *   主界面  : 大字时钟 HH:MM + 日期/温湿度/连接状态 + 模式参数 + 闹钟倒计时
 *   闹钟设置: HH:MM:SS 三段闪烁 + 按键提示
 */

/**
 * @brief 初始化 OLED + LVGL (I2C0, 地址 0x3C, SSD1306)
 */
void display_init(void);

/**
 * @brief 启动显示刷新 (时区设置 + LVGL 周期刷新定时器)
 */
void oled_task_init(void);

/**
 * @brief 更新 OLED 显示缓存 (LVGL 版本保留接口, 由定时器自动刷新)
 */
void display_update(const oled_data_t *data);

/**
 * @brief 关闭 OLED 显示 (进入低功耗前调用, 省 ~10mA)
 */
void display_suspend(void);

/**
 * @brief 重新开启 OLED 显示 (唤醒后调用)
 */
void display_resume(void);

#endif /* DISPLAY_H */
