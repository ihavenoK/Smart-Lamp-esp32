/**
 * @file key.c
 * @brief 按键组件 — 非阻塞去抖动 + 命令生成 + 长按检测
 *
 * MODE键 (GPIO13): 短按(<2s)切换模式 / 长按(>=2s)进入闹钟设置
 * ADJUST键 (GPIO14): 短按调节亮度/颜色 / 闹钟设置模式下进入下一步
 *
 * 2026-08-11 低功耗改造: 按键从 GPIO18/19 改接 RTC GPIO 13/14,
 *   电路反接为 "按下=高电平" (接 3.3V), 满足 ESP32 EXT1 ANY_HIGH 唤醒。
 *   内部下拉, 睡眠时按键悬空=低, 不会自唤醒。
 *
 * 按键按下 → lamp_cmd_t → g_cmd_queue → main_ctrl_task 处理
 * 50ms 定时器扫描, MODE 键区分短按(松手时触发)和长按(持续2s触发)
 *
 * 和 STM32 原版的区别:
 *   STM32 用阻塞 delay_ms(20) + while(按键松手) 消抖,
 *   ESP32 用 FreeRTOS 50ms 定时器非阻塞扫描, 不占 CPU。
 *
 * 触发时机:
 *   ADJUST: 按下即触发 (即时响应)
 *   MODE 短按: 松手时触发 (用于区分长按)
 *   MODE 长按: 按住 2000ms 触发 (进入闹钟设置)
 */

#include "key.h"
#include "main.h"
#include "lamp_core.h"     /* g_alarm_state, alarm_set_state_t */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "key";

#define KEY_MODE_GPIO       32U    /* RTC GPIO: 支持 EXT1 唤醒 (原 GPIO18, 修正 GPIO13→32 避免与雷达 RX 冲突) */
#define KEY_ADJUST_GPIO     14U    /* RTC GPIO: 支持 EXT1 唤醒 (原 GPIO19) */
#define DEBOUNCE_MS         50U
#define HOLD_SHORT_MS       500U    /* ADJUST/普通长按阈值 (暂未使用) */
#define HOLD_LONG_MS        2000U   /* MODE 进入闹钟设置的长按阈值 */

/* 外部句柄 */
extern QueueHandle_t g_cmd_queue;
extern uint8_t g_mode;
extern uint8_t g_light_level;
extern uint8_t g_color_index;

typedef struct {
    key_state_t state;
    uint32_t    press_tick;
    uint8_t     key_id;
    uint8_t     stable_count;
    uint8_t     last_level;
} key_ctx_t;

static key_ctx_t g_key1_ctx;
static key_ctx_t g_key2_ctx;
static TimerHandle_t g_scan_timer;

/* ====== 命令生成 ====== */

/**
 * @brief 生成按键命令并发送到命令队列
 *
 * 正常模式:
 *   MODE 短按 → 切换灯光模式 0→1→...→6→0
 *   ADJUST 短按 → 亮度+1 (自控模式则颜色+1)
 *
 * 闹钟设置模式:
 *   MODE 短按 → CMD_TYPE_ALARM_INC  (递增当前字段)
 *   ADJUST 短按 → CMD_TYPE_ALARM_STEP (进入下一步)
 */
static void key_action(uint8_t key_id)
{
    lamp_cmd_t cmd = {
        .source   = CMD_SRC_KEY,
        .mode     = g_mode,
        .light    = g_light_level,
        .color    = g_color_index,
        .cmd_type = CMD_TYPE_SET,
    };

    if (g_alarm_state != ALARM_SET_IDLE) {
        /* 闹钟设置模式: 按键含义和正常模式完全不同 */
        if (key_id == KEY_MODE) {
            cmd.cmd_type = CMD_TYPE_ALARM_INC;
            ESP_LOGD(TAG, "Alarm mode: INC");
        } else {
            cmd.cmd_type = CMD_TYPE_ALARM_STEP;
            ESP_LOGD(TAG, "Alarm mode: STEP");
        }
    } else {
        /* 正常模式: 沿用原有逻辑 */
        if (key_id == KEY_MODE) {
            cmd.mode = (g_mode + 1U) % LAMP_MODE_COUNT;
            ESP_LOGI(TAG, "MODE -> %u", cmd.mode);
        } else {
            if (g_mode == LAMP_MODE_AUTO) {
                cmd.color = (g_color_index + 1U) % 7U;
                ESP_LOGI(TAG, "ADJUST color -> %u", cmd.color);
            } else {
                cmd.light = (g_light_level + 1U) % 5U;
                ESP_LOGI(TAG, "ADJUST level -> %u", cmd.light);
            }
        }
    }

    xQueueSend(g_cmd_queue, &cmd, 0);
}

/* ====== 状态机 ====== */

/**
 * @brief 按键状态机处理 (50ms 周期调用)
 *
 * 状态流转:
 *   IDLE → (检测到按下) → DEBOUNCE → (连续 2 次确认) → PRESSED
 *   PRESSED → (松手) → RELEASE → (下一周期) → IDLE  【短按】
 *   PRESSED → (持续按住>=阈值) → HOLD → (松手) → RELEASE → IDLE 【长按】
 *
 * 关键改动 vs 旧版:
 *   - 短按动作从 PRESSED 移到 RELEASE, 松手才触发
 *   - 长按动作在进入 HOLD 时触发 (不等待松手)
 *   - MODE 键长按阈值 2000ms, 用于进入闹钟设置
 */
static void key_process(key_ctx_t *ctx)
{
    switch (ctx->state) {
    case KEY_STATE_IDLE:
        if (ctx->last_level) {
            ctx->state = KEY_STATE_DEBOUNCE;
            ctx->stable_count = 0U;
        }
        break;

    case KEY_STATE_DEBOUNCE:
        if (ctx->last_level) {
            ctx->stable_count++;
            if (ctx->stable_count >= 2U) {
                ctx->state       = KEY_STATE_PRESSED;
                ctx->press_tick  = xTaskGetTickCount();
                /* ADJUST 键即时响应 (按下即触发, 不改原有行为) */
                if (ctx->key_id == KEY_ADJUST) {
                    key_action(KEY_ADJUST);
                }
                /* MODE 键延迟到松手(短按)或超时(长按), 不在此触发 */
            }
        } else {
            ctx->state = KEY_STATE_IDLE;
        }
        break;

    case KEY_STATE_PRESSED:
        if (!ctx->last_level) {
            /* 松手 */
            if (ctx->key_id == KEY_MODE) {
                /* MODE 短按确认 → 仅在非 HOLD 路径时触发 */
                key_action(KEY_MODE);
            }
            /* ADJUST 松手不做任何事 (已在按下时触发) */
            ctx->state = KEY_STATE_RELEASE;
        } else if (ctx->key_id == KEY_MODE) {
            /* 仅 MODE 键需要长按检测 (进入闹钟设置) */
            if ((xTaskGetTickCount() - ctx->press_tick)
                >= pdMS_TO_TICKS(HOLD_LONG_MS)) {
                ctx->state = KEY_STATE_HOLD;

                /* MODE 长按 → 发送闹钟设置入口命令 */
                lamp_cmd_t alarm_cmd = {
                    .source   = CMD_SRC_KEY,
                    .cmd_type = CMD_TYPE_ALARM_ENTER,
                };
                xQueueSend(g_cmd_queue, &alarm_cmd, 0);
                ESP_LOGI(TAG, "MODE long press -> alarm setting");
            }
            /* ADJUST 键不进入 HOLD (按住不放只是重复触发, 由按键回弹决定) */
        }
        break;

    case KEY_STATE_HOLD:
        if (!ctx->last_level) {
            /* 长按后松手 → 不触发短按动作 (直接进入 RELEASE) */
            ctx->state = KEY_STATE_RELEASE;
        }
        break;

    case KEY_STATE_RELEASE:
        ctx->state = KEY_STATE_IDLE;
        break;

    default:
        ctx->state = KEY_STATE_IDLE;
        break;
    }
}

static void scan_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    /* 按键按下时 GPIO 为高电平 (外部接 3.3V, 内部下拉), last_level=1 表示按下 */
    g_key1_ctx.last_level = (gpio_get_level(KEY_MODE_GPIO) == 1U) ? 1U : 0U;
    g_key2_ctx.last_level = (gpio_get_level(KEY_ADJUST_GPIO) == 1U) ? 1U : 0U;
    key_process(&g_key1_ctx);
    key_process(&g_key2_ctx);
}

void key_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << KEY_MODE_GPIO) | (1ULL << KEY_ADJUST_GPIO),
        .mode         = GPIO_MODE_INPUT,
        /* 低功耗改造: 按键反接 3.3V (按下=高), 改用内部下拉
         * 睡眠时按键悬空=低电平, 不会误触发 EXT1 ANY_HIGH 唤醒 */
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    g_key1_ctx = (key_ctx_t){ .state = KEY_STATE_IDLE, .key_id = KEY_MODE };
    g_key2_ctx = (key_ctx_t){ .state = KEY_STATE_IDLE, .key_id = KEY_ADJUST };

    /* 50ms 定时器扫描, 替代阻塞的 delay_ms(20) */
    g_scan_timer = xTimerCreate("key_scan",
        pdMS_TO_TICKS(DEBOUNCE_MS), pdTRUE, NULL, scan_callback);
    xTimerStart(g_scan_timer, 0);

    ESP_LOGI(TAG, "Keys ready: MODE=GPIO%u ADJUST=GPIO%u", KEY_MODE_GPIO, KEY_ADJUST_GPIO);
}
