/**
 * @file main.c
 * @brief GlowMate SmartLamp - ESP32 主入口
 *
 * 移植自 STM32F103C8T6 + ESP8266 双芯片方案
 * 改为 ESP32-WROOM-32 单芯片 FreeRTOS 多任务架构
 *
 * 任务列表 (7个):
 *   - wifi_task      : WiFi 连接管理 + SNTP 北京时间同步
 *   - ble_uart_task  : BLE UART 透传 (Nordic NUS), 替代巴法云
 *   - main_ctrl_task : 核心业务逻辑 (模式/亮度/颜色/闹钟)
 *   - ws2812b_task   : WS2812B 灯带刷新 + 呼吸灯动画
 *   - sensor_task    : DHT11 + ADC + 红外传感器
 *   - oled_task      : OLED 12864 显示刷新
 *   - voice_task     : ASRPRO UART2 语音协议收发
 */

#include "main.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"          /* esp_reset_reason() */

static const char *TAG = "smartlamp";

/* ====== 全局句柄 ====== */
EventGroupHandle_t g_system_events      = NULL;
QueueHandle_t      g_cmd_queue          = NULL;
QueueHandle_t      g_sensor_queue       = NULL;
QueueHandle_t      g_ble_upload_queue   = NULL;

/* ====== 外部任务声明（组件初始化函数） ====== */
extern void wifi_task_init(void);
extern void ble_uart_init(void);       /* 主线程同步完成 NimBLE 初始化 */
extern void ble_uart_task_init(void);  /* 启动 BLE Host 任务 */
extern void alarm_init(void);          /* 闹钟定时器 (必须在 main_ctrl 前创建) */
extern void main_ctrl_task_init(void);
extern void ws2812b_task_init(void);
extern void sensor_task_init(void);
extern void oled_task_init(void);
extern void voice_task_init(void);

void app_main(void)
{
    /* 1. 初始化 NVS (WiFi凭证、用户设置) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 2. 创建全局句柄 */
    g_system_events     = xEventGroupCreate();
    g_cmd_queue         = xQueueCreate(16, sizeof(lamp_cmd_t));
    g_sensor_queue      = xQueueCreate(4,  sizeof(sensor_data_t));
    g_ble_upload_queue  = xQueueCreate(4,  sizeof(ble_state_t));

    /* 0. 打印复位原因 (规范 §7.2) */
    {
        esp_reset_reason_t reason = esp_reset_reason();
        const char *str;
        switch (reason) {
        case ESP_RST_POWERON:   str = "Power-on";          break;
        case ESP_RST_EXT:       str = "External pin";      break;
        case ESP_RST_SW:        str = "Software reset";    break;
        case ESP_RST_PANIC:     str = "Exception/panic";   break;
        case ESP_RST_INT_WDT:   str = "Interrupt WDT";     break;
        case ESP_RST_TASK_WDT:  str = "Task WDT";          break;
        case ESP_RST_WDT:       str = "Other WDT";         break;
        case ESP_RST_DEEPSLEEP: str = "Deep-sleep wake";   break;
        case ESP_RST_BROWNOUT:  str = "Brownout";          break;
        case ESP_RST_SDIO:      str = "SDIO";              break;
        default:                str = "Unknown";           break;
        }
        ESP_LOGI(TAG, "Reset reason: %s (%d)", str, (int)reason);
    }

    ESP_LOGI(TAG, "GlowMate SmartLamp starting...");

    /* 3. NimBLE 初始化 (必须在主线程同步完成, 不能放 FreeRTOS 任务) */
    ble_uart_init();

    /* 4. 创建各组件任务 */
    wifi_task_init();
    ble_uart_task_init();
    ws2812b_task_init();
    sensor_task_init();
    oled_task_init();
    voice_task_init();
    alarm_init();            /* 闹钟 + 学习定时器, 必须在 main_ctrl 之前创建 */
    main_ctrl_task_init();

    /* app_main 自身退化为空闲，FreeRTOS 调度器接管 */
    ESP_LOGI(TAG, "All tasks created, scheduler running.");
    vTaskDelete(NULL);
}
