/**
 * @file led_strip.c
 * @brief WS2812B 灯带硬件驱动 — ESP32 RMT + led_strip 组件
 *
 * 纯硬件层, 不包含业务逻辑。业务逻辑在 lamp_core.c。
 *
 * 和 STM32 版本的关键差异:
 *   STM32: TIM2 PWM+DMA1, 800kHz 手动构造波形, CPU 阻塞等 DMA
 *   ESP32: RMT 外设自动处理时序, led_strip_refresh() 后台传输, 不阻塞
 *
 * 注意: WS2812B 必须外部 5V 供电, 32 颗全白 ≈ 1.92A
 * 安装方式: idf.py add-dependency "espressif/led_strip"
 */

#include "ws2812b.h"          /* 我们自己的 API 声明 */
#include <led_strip.h>         /* 官方 led_strip 组件 (类型/函数) */
#include "main.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"   /* 互斥锁 */
#include "esp_log.h"

/* g_mode 定义在 lamp_core.c, 这里只声明引用 (避免循环依赖) */
extern uint8_t g_mode;

/* 传感器值 (由 main_ctrl_task 从 sensor_queue 消费后更新) */
extern uint8_t  g_ir_state;
extern uint16_t g_radar_dist;
extern uint8_t  g_radar_presence;
extern uint16_t g_adc_val;
extern uint8_t  g_color_index;

/* RMT 互斥锁: main_ctrl_task(prio8) 和 ws2812b_task(prio7) 不能同时发 RMT */
static SemaphoreHandle_t g_rmt_mutex = NULL;

static const char *TAG = "led_strip";
static led_strip_handle_t g_strip = NULL;

/* ====== 呼吸灯动画状态 ====== */
static uint8_t  g_breath_val;       /* 0-255 当前亮度 */
static uint8_t  g_breath_phase;     /* 0-13 颜色阶段 */

/* 呼吸灯步长:
 *   原 STM32: Color++ 每次+1, delay_ms(5), ~1.8s/相位, ~25s 完整循环
 *   ESP32 50Hz: 20ms/帧, 3 步长 → 86帧/相位 → ~1.7s/相位, ~24s 循环 */
#define BREATH_STEP  3U

/* ====== 颜色查表 (原 STM32 WS2812B.c) ====== */
typedef struct { uint8_t r, g, b; } rgb_t;

static const rgb_t normal_table[5] = {
    {0x00,0x00,0x00}, {0x44,0x44,0x11}, {0x77,0x77,0x11},
    {0xBB,0xBB,0x11}, {0xEE,0xEE,0x00},
};
static const rgb_t cold_table[5] = {
    {0x00,0x00,0x00}, {0x55,0x55,0x99}, {0x77,0x77,0xBB},
    {0x99,0x99,0xDD}, {0xBB,0xBB,0xFF},
};
static const rgb_t warm_table[5] = {
    {0x00,0x00,0x00}, {0x33,0x33,0x44}, {0x44,0x44,0x77},
    {0xAA,0xAA,0x88}, {0xEE,0xEE,0xFF},
};
static const rgb_t auto_color[7] = {
    {0xFF,0xFF,0xFF}, {0x00,0xFF,0xFF}, {0xFF,0xFF,0x00},
    {0xFF,0x00,0xFF}, {0x00,0x00,0xFF}, {0xFF,0x00,0x00},
    {0x00,0xFF,0x00},
};

/* ====== 公共 API ====== */

void led_strip_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num   = LED_STRIP_GPIO,
        .max_leds         = LED_STRIP_NUM,
        .led_model        = LED_MODEL_WS2812,
        .flags.invert_out = 0U,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src       = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10U * 1000U * 1000U,
        .flags.with_dma = 0U,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &g_strip));

    g_rmt_mutex = xSemaphoreCreateMutex();

    led_set_all(0U, 0U, 0U);
    led_refresh();

    ESP_LOGI(TAG, "WS2812B ready: GPIO%u, %u LEDs",
             LED_STRIP_GPIO, LED_STRIP_NUM);
}

void led_set_all(uint8_t r, uint8_t g, uint8_t b)
{
    if (g_strip == NULL) return;
    for (uint8_t i = 0U; i < LED_STRIP_NUM; i++) {
        ESP_ERROR_CHECK(led_strip_set_pixel(g_strip, i, r, g, b));
    }
}

void led_set_pixel(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)
{
    if (g_strip == NULL || idx >= LED_STRIP_NUM) return;
    ESP_ERROR_CHECK(led_strip_set_pixel(g_strip, idx, r, g, b));
}

void led_refresh(void)
{
    if (g_strip != NULL && g_rmt_mutex != NULL) {
        xSemaphoreTake(g_rmt_mutex, portMAX_DELAY);
        ESP_ERROR_CHECK(led_strip_refresh(g_strip));
        xSemaphoreGive(g_rmt_mutex);
    }
}

void led_set_normal(uint8_t level)
{
    if (level > 4U) level = 0U;
    const rgb_t *c = &normal_table[level];
    led_set_all(c->r, c->g, c->b);
}

void led_set_cold(uint8_t level)
{
    if (level > 4U) level = 0U;
    const rgb_t *c = &cold_table[level];
    led_set_all(c->r, c->g, c->b);
}

void led_set_warm(uint8_t level)
{
    if (level > 4U) level = 0U;
    const rgb_t *c = &warm_table[level];
    led_set_all(c->r, c->g, c->b);
}

void led_set_auto_color(uint8_t c, uint8_t brightness)
{
    if (c > 6U) c = 0U;
    const rgb_t *clr = &auto_color[c];
    uint8_t r = (uint8_t)(((uint16_t)clr->r * brightness) / 255U);
    uint8_t g = (uint8_t)(((uint16_t)clr->g * brightness) / 255U);
    uint8_t b = (uint8_t)(((uint16_t)clr->b * brightness) / 255U);
    led_set_all(r, g, b);
}

void led_breath_start(void)
{
    g_breath_val   = 0U;
    g_breath_phase = 0U;
}

void led_breath_step(void)
{
    /* 14 种颜色组合的呼吸灯渐变, 每帧一 step
     * 亮度从 0 递增到约 252, 到达阈值后换相并重置。
     * 相位的 "渐暗" 效果由 cv = 255 - v 实现 (偶数相用 v, 奇数相用 cv)。 */
    g_breath_val += BREATH_STEP;
    if (g_breath_val >= (255U - BREATH_STEP)) {
        g_breath_phase = (g_breath_phase + 1U) % 14U;
        g_breath_val = 0U;
    }

    /* 从原 STM32 ColorLight_Mode() 逐项搬运, 用 led_set_pixel 替代 WS2812B_send1 */
    uint8_t v = g_breath_val;
    uint8_t cv = 255U - v;
    uint8_t i;

    switch (g_breath_phase) {
    case 0:  for (i=0;i<32;i++) led_set_pixel(i, 0U,v,0U); break;
    case 1:  for (i=0;i<32;i++) led_set_pixel(i, 0U,cv,0U); break;
    case 2:  for (i=0;i<32;i++) led_set_pixel(i, v,0U,0U); break;
    case 3:  for (i=0;i<32;i++) led_set_pixel(i, cv,0U,0U); break;
    case 4:  for (i=0;i<32;i++) led_set_pixel(i, 0U,0U,v); break;
    case 5:  for (i=0;i<32;i++) led_set_pixel(i, 0U,0U,cv); break;
    case 6:  for (i=0;i<32;i++) led_set_pixel(i, v,v,0U); break;
    case 7:  for (i=0;i<32;i++) led_set_pixel(i, cv,cv,0U); break;
    case 8:  for (i=0;i<32;i++) led_set_pixel(i, 0U,v,v); break;
    case 9:  for (i=0;i<32;i++) led_set_pixel(i, 0U,cv,cv); break;
    case 10: for (i=0;i<32;i++) led_set_pixel(i, v,0U,v); break;
    case 11: for (i=0;i<32;i++) led_set_pixel(i, cv,0U,cv); break;
    case 12: for (i=0;i<32;i++) led_set_pixel(i, v,v,v); break;
    case 13: for (i=0;i<32;i++) led_set_pixel(i, cv,cv,cv); break;
    default: led_set_all(0U,0U,0U); break;
    }
}

/* ====== 刷新任务 (50Hz / 自动调频) ====== */
static void ws2812b_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "WS2812B task started.");

    /* vTaskDelayUntil 保持严格 50Hz 节奏, 消除 vTaskDelay 的累积抖动 */
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (1) {
        uint8_t mode = g_mode;

        if (mode == LAMP_MODE_COLOR) {
            /* 呼吸灯: 50Hz 严格节拍 */
            led_breath_step();
            led_refresh();
            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20));

        } else if (mode == LAMP_MODE_NIGHT) {
            /* 夜灯: 雷达 ≤ 200cm → 亮, 否则灭 */
            if (g_radar_dist > 0U && g_radar_dist <= 200U) {
                led_set_normal(1U);
            } else {
                led_set_all(0U, 0U, 0U);
            }
            led_refresh();
            vTaskDelay(pdMS_TO_TICKS(200));
            xLastWakeTime = xTaskGetTickCount();

        } else if (mode == LAMP_MODE_STUDY) {
            /* 学习灯: 雷达 ≤ 120cm → 亮, 否则灭 */
            if (g_radar_dist > 0U && g_radar_dist <= 120U) {
                led_set_normal(1U);
            } else {
                led_set_all(0U, 0U, 0U);
            }
            led_refresh();
            vTaskDelay(pdMS_TO_TICKS(200));
            xLastWakeTime = xTaskGetTickCount();

        } else if (mode == LAMP_MODE_AUTO) {
            /* 自控灯: 光敏ADC → 动态亮度 (0-4095 → 0-255)
             *   环境越亮(ADC高)→灯越亮(brightness高), 500ms刷新 */
            uint16_t adc = g_adc_val;
            /* 正比映射: ADC 0(最暗)→brightness 0(灭), ADC 4095(最亮)→brightness 255(最亮) */
            uint8_t brightness = (uint8_t)(((uint32_t)adc * 255U) / 4095U);
            led_set_auto_color(g_color_index, brightness);
            led_refresh();
            vTaskDelay(pdMS_TO_TICKS(500));
            xLastWakeTime = xTaskGetTickCount();

        } else {
            /* 静态模式 (NORMAL/COLD/WARM):
             *   颜色已在 lamp_mode_set 中设置, 不需刷新 */
            vTaskDelay(pdMS_TO_TICKS(500));
            xLastWakeTime = xTaskGetTickCount();
        }
    }
}

void ws2812b_task_init(void)
{
    led_strip_init();
    xTaskCreate(ws2812b_task, "ws2812b", 3072, NULL, 7, NULL);
}
