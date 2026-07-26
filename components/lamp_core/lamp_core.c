/**
 * @file lamp_core.c
 * @brief 核心业务逻辑 — 模式切换/亮度调整, 对应原 STM32 main.c while(1)
 */

#include "lamp_core.h"
#include "main.h"
#include "ws2812b.h"
#include "alarm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_task_wdt.h"       /* TWDT 看门狗 */

static const char *TAG = "lamp_core";

/* ====== 全局状态变量 (定义) ====== */
uint8_t  g_mode         = 0U;
uint8_t  g_light_level  = 4U;
uint8_t  g_color_index  = 0U;
uint8_t  g_temp         = 0U;
uint8_t  g_humi         = 0U;
uint16_t g_study_time   = 0U;

/* 传感器原始值 (供 ws2812b_task 驱动 NIGHT/STUDY/AUTO 模式) */
uint8_t  g_ir_state        = 0U;
uint16_t g_radar_dist      = 0U;
uint8_t  g_radar_presence  = 0U;
uint16_t g_adc_val         = 0U;

uint8_t  g_hour = 0U, g_min = 0U, g_sec = 0U;
uint32_t g_alarm_time  = 0U;
uint8_t  g_flag_count  = 0U;

/* ====== Ligth_Set 移植 (原 STM32 WS2812B.c 末尾) ====== */

void lamp_mode_set(uint8_t mode, uint8_t level, uint8_t color)
{
    /* 参数边界检查 (防御性编程) */
    if (mode >= LAMP_MODE_COUNT) mode = 0U;
    if (level > 4U)              level = 0U;
    if (color > 13U)             color = 0U;

    /* 保存旧模式: 切换亮度/颜色时 mode 不变,
     * 需区分「模式切换」和「模式内调节」,
     * 避免重置呼吸动画/学习计时器等运行中状态 */
    uint8_t old_mode = g_mode;

    /* 更新全局状态 */
    g_mode        = mode;
    g_light_level = level;
    g_color_index = color;

    /* 按模式分发 (完全对应原 STM32 Ligth_Set 的 switch-case) */
    switch (mode) {

    case LAMP_MODE_NORMAL:  /* 0: 常规灯 */
        study_timer_stop();
        g_study_time = 0U;
        led_set_normal(level);
        break;

    case LAMP_MODE_COLD:    /* 1: 冷光灯 */
        study_timer_stop();
        g_study_time = 0U;
        led_set_cold(level);
        break;

    case LAMP_MODE_WARM:    /* 2: 暖光灯 */
        study_timer_stop();
        g_study_time = 0U;
        led_set_warm(level);
        break;

    case LAMP_MODE_COLOR:   /* 3: 氛围灯 (呼吸) */
        study_timer_stop();
        g_study_time = 0U;
        /* 仅在真正切换进此模式时重置呼吸动画,
         * 模式内切亮度不打断动画 */
        if (old_mode != LAMP_MODE_COLOR) {
            led_breath_start();
        }
        break;

    case LAMP_MODE_NIGHT:   /* 4: 夜灯 */
        study_timer_stop();
        g_study_time = 0U;
        /* 红外感应由 ws2812b_task 处理 */
        break;

    case LAMP_MODE_STUDY:   /* 5: 学习灯 */
        /* 仅在真正切换进此模式时启动学习计时器,
         * 模式内切亮度不重置计时 */
        if (old_mode != LAMP_MODE_STUDY) {
            study_timer_start();
        }
        /* 红外感应由 ws2812b_task 处理 */
        break;

    case LAMP_MODE_AUTO:    /* 6: 自控灯 */
        study_timer_stop();
        g_study_time = 0U;
        /* ADC 值来自 sensor_task, ws2812b_task 动态更新 */
        led_set_auto_color(color, 128U);  /* 初始中等亮度 */
        break;

    default:
        led_set_all(0U, 0U, 0U);
        break;
    }

    led_refresh();
    ESP_LOGI(TAG, "Mode: %u Level: %u Color: %u", mode, level, color);
}

/* ====== 闹钟设置状态机 ====== */

/* 10 秒无操作自动退出闹钟设置 */
#define ALARM_SET_TIMEOUT_MS  10000U

alarm_set_state_t g_alarm_state = ALARM_SET_IDLE;
uint8_t g_alarm_hour = 0U, g_alarm_min = 0U, g_alarm_sec = 0U;
static TickType_t g_alarm_last_tick = 0U;

/**
 * @brief 处理闹钟设置模式下的按键命令
 *
 * MODE 键 = 递增当前字段 (小时/分/秒), 确认阶段 = 取消
 * ADJUST 键 = 确认当前字段并进入下一步, 确认阶段 = 生效闹钟
 *
 * @param cmd_type  命令类型 (CMD_TYPE_ALARM_INC / CMD_TYPE_ALARM_STEP)
 */
static void alarm_setting_handle(uint8_t cmd_type)
{
    if (cmd_type == CMD_TYPE_ALARM_INC) {
        /* MODE 键: 递增当前字段 */
        switch (g_alarm_state) {
        case ALARM_SET_HOUR:
            g_alarm_hour = (g_alarm_hour + 1U) % 24U;
            break;
        case ALARM_SET_MIN:
            g_alarm_min = (g_alarm_min + 1U) % 60U;
            break;
        case ALARM_SET_SEC:
            g_alarm_sec = (g_alarm_sec + 1U) % 60U;
            break;
        case ALARM_SET_CONFIRM:
            /* 确认阶段按 MODE = 取消 */
            ESP_LOGI(TAG, "Alarm setting cancelled");
            g_alarm_state = ALARM_SET_IDLE;
            return;
        default: break;
        }
    } else if (cmd_type == CMD_TYPE_ALARM_STEP) {
        /* ADJUST 键: 进入下一阶段 */
        switch (g_alarm_state) {
        case ALARM_SET_HOUR:
            g_alarm_state = ALARM_SET_MIN;
            break;
        case ALARM_SET_MIN:
            g_alarm_state = ALARM_SET_SEC;
            break;
        case ALARM_SET_SEC:
            g_alarm_state = ALARM_SET_CONFIRM;
            break;
        case ALARM_SET_CONFIRM:
            /* 确认生效 */
            {
                uint32_t total_sec = (uint32_t)g_alarm_hour * 3600U
                                   + (uint32_t)g_alarm_min * 60U
                                   + (uint32_t)g_alarm_sec;
                if (total_sec > 0U) {
                    alarm_start(total_sec);
                    ESP_LOGI(TAG, "Alarm set: %uh %um %us",
                             g_alarm_hour, g_alarm_min, g_alarm_sec);
                } else {
                    ESP_LOGW(TAG, "Alarm time is 0, ignored");
                }
                g_alarm_state = ALARM_SET_IDLE;
            }
            return;
        default: break;
        }
    }

    /* 同步闹钟设定值到 g_hour/g_min/g_sec,
     * OLED 显示任务从中读取待显示的值 */
    g_hour = g_alarm_hour;
    g_min  = g_alarm_min;
    g_sec  = g_alarm_sec;
}

/* ====== 主控制任务 ====== */

static void main_ctrl_task(void *arg)
{
    (void)arg;
    lamp_cmd_t cmd;
    sensor_data_t sensor;

    /* 注册到任务看门狗 (TWDT), 超时 5 秒由 sdkconfig 配置 */
    esp_task_wdt_add(NULL);
    ESP_LOGI(TAG, "Main control task started (TWDT registered).");

    /* 开机默认: 模式 0 (常规灯), 亮度 4 (最亮), 颜色 0 */
    lamp_mode_set(0U, 4U, 0U);
    ESP_LOGI(TAG, "LED refresh done");

    /* BLE 上报缓存: 初始化为不可能值, 确保首帧必上报 */
    TickType_t last_ble_upload = xTaskGetTickCount();

    while (1) {
        /* ====== 闹钟设置超时检测 (每次循环都检查, 不依赖按键触发) ====== */
        if (g_alarm_state != ALARM_SET_IDLE) {
            if ((xTaskGetTickCount() - g_alarm_last_tick)
                >= pdMS_TO_TICKS(ALARM_SET_TIMEOUT_MS)) {
                ESP_LOGI(TAG, "Alarm setting timeout");
                g_alarm_state = ALARM_SET_IDLE;
            }
        }

        /* 接收命令 (按键/语音/BLE统一入口) */
        if (xQueueReceive(g_cmd_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {

            /* -------------------------------------------
             * 优先级 1: 闹钟设置模式 (模态对话框, 拦截所有按键)
             * ------------------------------------------- */
            if (g_alarm_state != ALARM_SET_IDLE) {

                /* 仅处理闹钟相关命令, 其他命令丢弃 */
                if (cmd.cmd_type == CMD_TYPE_ALARM_INC
                    || cmd.cmd_type == CMD_TYPE_ALARM_STEP) {
                    g_alarm_last_tick = xTaskGetTickCount();
                    alarm_setting_handle(cmd.cmd_type);
                }
                continue;
            }

            /* -------------------------------------------
             * 优先级 2: 长按 MODE 进入闹钟设置
             * ------------------------------------------- */
            if (cmd.cmd_type == CMD_TYPE_ALARM_ENTER) {
                g_alarm_state  = ALARM_SET_HOUR;
                g_alarm_hour   = 0U;
                g_alarm_min    = 0U;
                g_alarm_sec    = 0U;
                g_hour = 0U; g_min = 0U; g_sec = 0U;
                g_alarm_last_tick = xTaskGetTickCount();
                ESP_LOGI(TAG, "Enter alarm setting mode");
                continue;
            }

            /* -------------------------------------------
             * 优先级 3: 正常模式 — 模式/亮度/颜色命令
             * ------------------------------------------- */
            ESP_LOGD(TAG, "CMD: src=%u mode=%u light=%u color=%u type=%u",
                     cmd.source, cmd.mode, cmd.light, cmd.color, cmd.cmd_type);

            /* 语音命令的亮度+/亮度- (cmd_type=1/2) */
            if (cmd.cmd_type == CMD_TYPE_LIGHT_UP) {
                uint8_t new_light = g_light_level + 1U;
                if (new_light > 4U) new_light = 4U;
                lamp_mode_set(g_mode, new_light, g_color_index);
            } else if (cmd.cmd_type == CMD_TYPE_LIGHT_DOWN) {
                uint8_t new_light = (g_light_level > 0U)
                                    ? g_light_level - 1U : 0U;
                lamp_mode_set(g_mode, new_light, g_color_index);
            } else {
                /* 默认: 直接设置模式 (CMD_TYPE_SET=0) */
                lamp_mode_set(cmd.mode, cmd.light, cmd.color);
            }
        }

        /* 读取传感器数据 (非阻塞) */
        if (xQueueReceive(g_sensor_queue, &sensor, 0) == pdTRUE) {
            g_temp           = sensor.temp;
            g_humi           = sensor.humi;
            g_ir_state       = sensor.ir_state;
            g_adc_val        = sensor.adc_val;
            g_radar_dist     = sensor.radar_dist;
            g_radar_presence = sensor.radar_presence;
        }

        /* BLE 状态定时上报: 每 2 秒推送一次到手机
         * 6 字节数据量极小, 无需脏检测, 简单轮询即可 */
        if ((xTaskGetTickCount() - last_ble_upload) >= pdMS_TO_TICKS(2000)) {
            ble_state_t state = {
                .mode       = g_mode,
                .light      = g_light_level,
                .color      = g_color_index,
                .temp       = g_temp,
                .humi       = g_humi,
                .study_time = (uint8_t)(g_study_time > 255U ? 255U : g_study_time),
            };
            xQueueSend(g_ble_upload_queue, &state, 0);
            last_ble_upload = xTaskGetTickCount();
        }

        /* 喂狗: 所有工作完成后重置 TWDT (规范 §7.1 — 单点集中喂狗) */
        esp_task_wdt_reset();
    }
}

void main_ctrl_task_init(void)
{
    xTaskCreate(main_ctrl_task, "main_ctrl", 4096, NULL, 8, NULL);
}
