/**
 * @file lamp_pm.c
 * @brief 低功耗管理 — Light Sleep + RTC 时钟备份
 *
 * 触发方: lamp_core 的 main_ctrl_task (闲置 N 秒后调用 lamp_pm_enter_sleep)
 * 唤醒源: EXT1 GPIO13/14 (ANY_HIGH, 按键反接 3.3V 按下=高)
 *        + 30 分钟兜底定时器
 *
 * 注意:
 *   - ESP32 (非 S2/S3) 的 EXT1 仅支持 ANY_HIGH/ALL_LOW,
 *     按键必须反接为 "按下=高电平", 睡眠时内部下拉=低 不自醒
 *   - GPIO13/14 是 RTC GPIO, GPIO18/19 不是, 唤醒必须落在 RTC GPIO 上
 *   - 进入 Light Sleep 前必须先灭灯 (WS2812B 保持供电, 不灭则白耗电)
 *     和关 OLED (SSD1306 持续刷新耗电 ~10mA)
 */

#include "lamp_pm.h"
#include "ws2812b.h"
#include "display.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = "lamp_pm";

/* ====== 唤醒源 GPIO (RTC GPIO, 与 key.c 一致) ====== */
#define PM_WAKE_KEY_MODE   32U    /* MODE 键 (GPIO13 已被雷达 RX 占用, 改用 32) */
#define PM_WAKE_KEY_ADJUST 14U    /* ADJUST 键 */

/* 兜底定时器: 30 分钟自动唤醒 (防止唤醒源失效导致死睡) */
#define PM_WAKE_TIMEOUT_US (30UL * 60UL * 1000000UL)

/* ====== RTC_SLOW_MEM 时钟备份 (RTC_NOINIT: 复位后保留) ====== */
#define PM_RTC_MAGIC 0x5A5AA55AU

typedef struct {
    uint32_t magic;      /* 有效性标记 */
    time_t   base_time;  /* 入睡时 Unix 时间戳 */
} pm_rtc_data_t;

RTC_NOINIT_ATTR static pm_rtc_data_t s_rtc_data;

void lamp_pm_init(void)
{
    /* EXT1: 按键高电平唤醒 (GPIO13/14 内部下拉, 按下接 3.3V 为高)
     * ESP32 仅支持 ANY_HIGH/ALL_LOW, 按键反接后 ANY_HIGH 正确 */
    const uint64_t wake_mask = (1ULL << PM_WAKE_KEY_MODE)
                             | (1ULL << PM_WAKE_KEY_ADJUST);
    esp_err_t ret = esp_sleep_enable_ext1_wakeup(wake_mask,
                                                 ESP_EXT1_WAKEUP_ANY_HIGH);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "EXT1 wakeup config failed: %s", esp_err_to_name(ret));
        return;
    }

    /* 兜底定时器: 防止所有唤醒源失效导致死睡 */
    ret = esp_sleep_enable_timer_wakeup(PM_WAKE_TIMEOUT_US);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Timer wakeup config failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Wake sources: GPIO%u/GPIO%u (EXT1) + timer %lus",
             PM_WAKE_KEY_MODE, PM_WAKE_KEY_ADJUST,
             (unsigned long)(PM_WAKE_TIMEOUT_US / 1000000UL));
}

void lamp_pm_save_time(void)
{
    time(&s_rtc_data.base_time);
    s_rtc_data.magic = PM_RTC_MAGIC;
    ESP_LOGI(TAG, "Time saved to RTC mem: %ld", (long)s_rtc_data.base_time);
}

time_t lamp_pm_restore_time(void)
{
    time_t now = time(NULL);

    /* 系统时间正常 (Light Sleep 下 RTC 硬件持续计时) → 无需恢复 */
    if (s_rtc_data.magic == PM_RTC_MAGIC
        && now < s_rtc_data.base_time) {
        /* 系统时间异常 (如未来 Deep Sleep 唤醒后归零) → 用备份兜底 */
        struct timeval tv = {
            .tv_sec  = s_rtc_data.base_time,
            .tv_usec = 0,
        };
        settimeofday(&tv, NULL);
        ESP_LOGW(TAG, "System time abnormal, restored from RTC backup: %ld",
                 (long)s_rtc_data.base_time);
        s_rtc_data.magic = 0U;  /* 一次性恢复 */
        return s_rtc_data.base_time;
    }

    ESP_LOGI(TAG, "System time intact (Light Sleep), no restore needed");
    return now;
}

void lamp_pm_enter_sleep(void)
{
    /* 1. 灭灯: WS2812B 上电保持, 睡眠期间必须主动熄灭 */
    led_set_all(0U, 0U, 0U);
    led_refresh();

    /* 2. 关 OLED: 停止 SSD1306 刷新 (省 ~10mA) */
    display_suspend();

    /* 3. 备份时间到 RTC_SLOW_MEM */
    lamp_pm_save_time();

    /* 4. 进入 Light Sleep (阻塞, 直到 GPIO/定时器唤醒) */
    ESP_LOGI(TAG, "Entering light sleep...");
    esp_light_sleep_start();

    /* 5. 唤醒: 打印唤醒原因 */
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    switch (cause) {
    case ESP_SLEEP_WAKEUP_EXT1:
        ESP_LOGI(TAG, "Wakeup from GPIO (EXT1)");
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        ESP_LOGI(TAG, "Wakeup from timer");
        break;
    default:
        ESP_LOGI(TAG, "Wakeup cause: %d", (int)cause);
        break;
    }

    /* 6. 恢复时间 + 重开 OLED */
    lamp_pm_restore_time();
    display_resume();
}
