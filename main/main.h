#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>

/* ====== 系统事件标志位 ====== */
#define EVT_WIFI_CONNECTED    (1UL << 0)
#define EVT_SENSOR_UPDATED    (1UL << 2)
#define EVT_CMD_RECEIVED      (1UL << 3)
#define EVT_TIME_SYNCED       (1UL << 4)
#define EVT_BLE_CONNECTED     (1UL << 5)

/* ====== 命令来源 ====== */
#define CMD_SRC_KEY           0x01U
#define CMD_SRC_VOICE         0x02U
#define CMD_SRC_BLE           0x03U   /* 原 CMD_SRC_CLOUD 改为 BLE */

/* ====== 命令类型 ====== */
#define CMD_TYPE_SET          0x00U   /* 直接设置 */
#define CMD_TYPE_LIGHT_UP     0x01U   /* 亮度+ */
#define CMD_TYPE_LIGHT_DOWN   0x02U   /* 亮度- */
#define CMD_TYPE_ALARM_ENTER  0x03U   /* 长按MODE进入闹钟设置 */
#define CMD_TYPE_ALARM_INC    0x04U   /* 闹钟设置中MODE递增 */
#define CMD_TYPE_ALARM_STEP   0x05U   /* 闹钟设置中ADJUST下一步 */
#define CMD_TYPE_ALARM_SET    0x06U   /* BLE直设闹钟: mode=时, light=分, color=秒 (0:0:0=取消) */

/* ====== 闹钟设置状态 (多组件共用, 定义在lamp_core.c) ====== */
typedef enum {
    ALARM_SET_IDLE    = 0,
    ALARM_SET_HOUR    = 1,
    ALARM_SET_MIN     = 2,
    ALARM_SET_SEC     = 3,
    ALARM_SET_CONFIRM = 4,
} alarm_set_state_t;

/* ====== 灯光模式 ====== */
#define LAMP_MODE_NORMAL      0U
#define LAMP_MODE_COLD        1U
#define LAMP_MODE_WARM        2U
#define LAMP_MODE_COLOR       3U
#define LAMP_MODE_NIGHT       4U
#define LAMP_MODE_STUDY       5U
#define LAMP_MODE_AUTO        6U
#define LAMP_MODE_COUNT       7U

/* ====== 统一命令结构体 ====== */
typedef struct {
    uint8_t source;     /* CMD_SRC_KEY / CMD_SRC_VOICE / CMD_SRC_BLE */
    uint8_t mode;       /* 灯光模式 0-6 */
    uint8_t light;      /* 亮度档位 0-4 */
    uint8_t color;      /* 颜色编号 0-13 */
    uint8_t cmd_type;   /* CMD_TYPE_SET / LIGHT_UP / LIGHT_DOWN */
} lamp_cmd_t;

/* ====== 传感器数据 ====== */
typedef struct {
    uint8_t  temp;            /* 温度 (0-50) */
    uint8_t  humi;            /* 湿度 (20-90) */
    uint16_t adc_val;         /* 光敏ADC值 (0-4095) */
    uint8_t  ir_state;        /* LD2402 IO引脚 0/1 */
    uint16_t radar_dist;      /* LD2402 UART 距离(cm), 0=无人 */
    uint8_t  radar_presence;  /* 检测状态: 0=无人 1=运动 2=静止 */
} sensor_data_t;

/* ====== OLED显示数据 ====== */
typedef struct {
    uint8_t  hour, min;           /* 当前时间 */
    uint8_t  alarm_h, alarm_m, alarm_s;  /* 闹钟倒计时 */
    uint8_t  flag_count;          /* 计时标志 */
    uint8_t  temp, humi;          /* 温湿度 */
    uint8_t  mode, light, color;  /* 灯状态 */
    uint16_t study_time;          /* 学习时长(分钟) */
} oled_data_t;

/* ====== BLE 状态上报数据 ====== */
typedef struct {
    uint8_t  mode;        /* 灯光模式 0-6 */
    uint8_t  light;       /* 亮度 0-4 */
    uint8_t  color;       /* 颜色 0-6 */
    uint8_t  temp;        /* 温度 */
    uint8_t  humi;        /* 湿度 */
    uint8_t  study_time;  /* 学习计时(分钟) */
} ble_state_t;

/* ====== 全局句柄 ====== */
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

extern EventGroupHandle_t g_system_events;
extern QueueHandle_t g_cmd_queue;
extern QueueHandle_t g_sensor_queue;
extern QueueHandle_t g_ble_upload_queue;
extern QueueHandle_t g_ble_upload_queue;

#endif /* MAIN_H */
