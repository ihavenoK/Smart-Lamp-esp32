/**
 * @file radar.c
 * @brief LD2402 毫米波人体存在传感器 UART 驱动
 *
 * 根据 HLK-LD2402 用户手册 V1.08 (海凌科) 实现。
 *
 * 通信协议:
 *   - 串口 TTL 电平, 波特率 115200, 8N1, 无流控
 *   - 出厂固件正常工作模式通过串口输出 ASCII 文本:
 *       无人: "OFF\r\n"
 *       有人: "distance: XXX\r\n" (XXX 为 cm 距离)
 *   - 刷新周期 165ms
 *
 * 实现策略:
 *   - 独立 FreeRTOS 任务轮询 UART RX (优先级 3, 栈 2KB)
 *   - 按行解析, 提取距离和状态存入静态变量
 *   - sensor_task 每 2s 通过 radar_get_distance() / radar_get_presence() 读取
 *
 * 工程模式 (未激活):
 *   - 可通过命令 0x0012 切换到工程模式, 输出各距离门能量值
 *   - 细节参见手册第 5 章通信协议
 */

#include "radar.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <stdlib.h>   /* atoi */

static const char *TAG = "radar";

/* 缓存最新数据, sensor_task 通过访问器读取 */
static uint16_t     s_distance  = 0U;
static uint8_t      s_presence  = RADAR_STATE_NONE;
static portMUX_TYPE s_lock      = portMUX_INITIALIZER_UNLOCKED;

/* 行缓冲区 — LD2402 输出最长 "distance: 1000\r\n" (18 字节), 64 字节足够 */
#define RADAR_LINE_BUF  64U
static char    s_buf[RADAR_LINE_BUF];
static uint8_t s_idx = 0U;

/* ====== 行解析 ====== */

/**
 * @brief 解析 LD2402 ASCII 输出
 *
 * "OFF" → 无人 (距离清零)
 * "distance：XX" 或 "distance: XX" → 有人 (距离单位 cm)
 *
 * 注意: 手册用的中文全角冒号 (：= 0xEFBC9A), 与 ASCII : (0x3A) 不同,
 * 因此不依赖 sscanf 格式串, 改为跳过 "distance" 后找第一个数字开始 atoi。
 */
static void radar_parse(const char *line)
{
    if (line[0] == 'O' && line[1] == 'F' && line[2] == 'F') {
        portENTER_CRITICAL(&s_lock);
        s_distance  = 0U;
        s_presence  = RADAR_STATE_NONE;
        portEXIT_CRITICAL(&s_lock);
        return;
    }

    if (line[0] == 'd') {
        /* 跳过 "distance", 然后跳过冒号和空格, 找到第一个数字 */
        const char *p = line + 8;   /* strlen("distance") = 8 */
        while (*p && (*p < '0' || *p > '9')) p++;
        int dist = atoi(p);
        if (dist > 0 && dist <= 1000) {
            portENTER_CRITICAL(&s_lock);
            s_distance  = (uint16_t)dist;
            s_presence  = RADAR_STATE_MOVING;
            portEXIT_CRITICAL(&s_lock);
        }
    }
}

/* ====== 雷达读取任务 ====== */

static void radar_task(void *arg)
{
    ESP_LOGI(TAG, "Reader started on UART%d (TX=GPIO%u RX=GPIO%u, %ubps)",
             RADAR_UART_NUM, RADAR_GPIO_TX, RADAR_GPIO_RX, RADAR_UART_BAUD);

    uint32_t idle_ticks = 0U;   /* 连续无数据计数, 用于超时告警 */

    while (1) {
        uint8_t ch;
        int len = uart_read_bytes(RADAR_UART_NUM, &ch, 1, pdMS_TO_TICKS(100));
        if (len <= 0) {
            /* 每 ~10 秒无数据复报一次, 确认模块状态 */
            if (++idle_ticks >= 100U) {
                ESP_LOGW(TAG, "No UART data for %.1fs! Check wiring (T→GPIO%d) / 3.3V power",
                         (float)idle_ticks * 0.1f, RADAR_GPIO_RX);
                idle_ticks = 0U;   /* 重置, 继续周期性告警 */
            }
            continue;
        }
        idle_ticks = 0U;   /* 有数据, 清零 */

        if (ch == '\r') continue;

        if (ch == '\n') {
            if (s_idx > 0U) {
                s_buf[s_idx] = '\0';
                radar_parse(s_buf);
                ESP_LOGD(TAG, "'%s' → d=%u st=%u", s_buf, s_distance, s_presence);
                s_idx = 0U;
            }
            continue;
        }

        /* 丢弃过长行, 防止缓冲区溢出 */
        if (s_idx < RADAR_LINE_BUF - 1U) {
            s_buf[s_idx++] = (char)ch;
        } else {
            ESP_LOGW(TAG, "Line overflow, discarding");
            s_idx = 0U;
        }
    }
}

/* ====== 初始化 ====== */

void radar_init(void)
{
    const uart_config_t uart_cfg = {
        .baud_rate  = RADAR_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(RADAR_UART_NUM, 512, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(RADAR_UART_NUM, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(RADAR_UART_NUM,
                                  RADAR_GPIO_TX,
                                  RADAR_GPIO_RX,
                                  UART_PIN_NO_CHANGE,
                                  UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "LD2402 radar init OK");
}

void radar_task_init(void)
{
    xTaskCreate(radar_task, "radar", 2560, NULL, 3, NULL);
}

/* ====== 数据访问器 (临界区保护, sensor_task 上下文调用) ====== */

uint16_t radar_get_distance(void)
{
    uint16_t d;
    portENTER_CRITICAL(&s_lock);
    d = s_distance;
    portEXIT_CRITICAL(&s_lock);
    return d;
}

uint8_t radar_get_presence(void)
{
    uint8_t p;
    portENTER_CRITICAL(&s_lock);
    p = s_presence;
    portEXIT_CRITICAL(&s_lock);
    return p;
}
