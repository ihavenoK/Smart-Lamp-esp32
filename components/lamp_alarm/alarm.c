/**
 * @file alarm.c
 * @brief 闹钟组件 — FreeRTOS 软件定时器
 *
 * 替代原 STM32:
 *   - RTC 硬件闹钟 (MyRTC.c) -> FreeRTOS Timer
 *   - TIM4 1秒学习计时 (timer.c) -> FreeRTOS Timer
 */

#include "alarm.h"
#include "main.h"
#include "lamp_core.h"
#include "voice.h"               /* voice_send_event() — 闹钟响铃通知 ASRPRO */
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_log.h"

static const char *TAG = "alarm";

static TimerHandle_t g_alarm_timer  = NULL;
static TimerHandle_t g_study_timer  = NULL;

static void alarm_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    ESP_LOGI(TAG, "Alarm ring!");

    g_flag_count = 0U;
    g_alarm_time = 0U;
    g_hour = 0U; g_min = 0U; g_sec = 0U;

    /* 通知 ASRPRO 播报闹钟铃声 (4字节帧: 0xFF 0x04 0xFE 0xEE) */
    voice_send_event(VOICE_EVT_ALARM_RING);
}

static void study_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    g_study_time++;
}

void alarm_init(void)
{
    /* 创建闹钟定时器 (不启动) */
    g_alarm_timer = xTimerCreate("alarm",
        pdMS_TO_TICKS(1000),    /* 默认1秒, 实际用 alarm_start 覆盖 */
        pdFALSE,                /* 一次性 */
        NULL,
        alarm_callback);

    /* 创建学习计时器 (不启动) */
    g_study_timer = xTimerCreate("study",
        pdMS_TO_TICKS(60000),   /* 60秒 */
        pdTRUE,                 /* 自动重载 */
        NULL,
        study_callback);

    ESP_LOGI(TAG, "Alarm initialized.");
}

void alarm_start(uint32_t seconds)
{
    if (g_alarm_timer == NULL) return;

    xTimerStop(g_alarm_timer, 0);
    xTimerChangePeriod(g_alarm_timer, pdMS_TO_TICKS(seconds * 1000U), 0);
    xTimerStart(g_alarm_timer, 0);
    g_flag_count = 1U;
    g_alarm_time = seconds;

    ESP_LOGI(TAG, "Alarm set: %lu seconds", (unsigned long)seconds);
}

void alarm_stop(void)
{
    if (g_alarm_timer == NULL) return;
    xTimerStop(g_alarm_timer, 0);
    g_flag_count = 0U;
}

uint32_t alarm_get_remaining(void)
{
    if (g_alarm_timer == NULL || !alarm_is_running()) {
        return 0U;
    }
    TickType_t remaining = xTimerGetExpiryTime(g_alarm_timer) - xTaskGetTickCount();
    /* ticks → ms → seconds: remaining * (ms_per_tick) / 1000 */
    return (uint32_t)((remaining * portTICK_PERIOD_MS) / 1000U);
}

uint8_t alarm_is_running(void)
{
    return g_flag_count;
}

void study_timer_start(void)
{
    if (g_study_timer != NULL) {
        xTimerStart(g_study_timer, 0);
    }
}

void study_timer_stop(void)
{
    if (g_study_timer != NULL) {
        xTimerStop(g_study_timer, 0);
    }
}
