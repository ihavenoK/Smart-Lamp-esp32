#ifndef LED_STRIP_H
#define LED_STRIP_H

#include <stdint.h>

/* 引脚和数量 */
#define LED_STRIP_GPIO      25U
#define LED_STRIP_NUM       32U

/* 初始化 + 任务创建 */
void led_strip_init(void);
void ws2812b_task_init(void);

/* 硬件控制 */
void led_set_all(uint8_t r, uint8_t g, uint8_t b);
void led_set_pixel(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
void led_refresh(void);

/* 各模式颜色设置 (被 lamp_core 调用) */
void led_set_normal(uint8_t level);
void led_set_cold(uint8_t level);
void led_set_warm(uint8_t level);
void led_set_auto_color(uint8_t color, uint8_t brightness);

/* 呼吸灯动画 */
void led_breath_start(void);
void led_breath_step(void);

#endif
