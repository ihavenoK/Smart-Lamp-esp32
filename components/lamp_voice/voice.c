/**
 * @file voice.c
 * @brief ASRPRO 语音模块 UART2 驱动 — 4字节协议收发
 *
 * 对应原 STM32 usart.c USART2 中断帧解析 + main.c switch(ASR_Code)
 * GPIO16=U2RXD, GPIO17=U2TXD (Datasheet 确认)
 */

#include "voice.h"
#include "main.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "voice";

/* 4字节帧状态机 */
#define FRAME_HEADER    0xFFU
#define FRAME_TAIL1     0xFEU
#define FRAME_TAIL2     0xEEU

typedef enum {
    FRAME_STATE_IDLE    = 0,
    FRAME_STATE_GOT_FF  = 1,
    FRAME_STATE_GOT_CMD = 2,
    FRAME_STATE_GOT_FE  = 3,
} frame_state_t;

/* 22条语音命令查表 */
typedef struct {
    uint8_t code;
    uint8_t mode;
    uint8_t light;
    uint8_t color;
    uint8_t special;
} voice_cmd_t;

static const voice_cmd_t voice_table[] = {
    {1,  0, 4, 0, 0},  /* 打开台灯 */
    {2,  0, 0, 0, 0},  /* 关闭台灯 */
    {3,  0, 4, 0, 0},  /* 最大亮度 */
    {4,  1, 4, 0, 0},  /* 冷光模式 */
    {5,  2, 4, 0, 0},  /* 暖光模式 */
    {6,  3, 0, 0, 0},  /* 彩光模式 */
    {7,  4, 2, 0, 0},  /* 夜灯模式 */
    {8,  5, 4, 0, 0},  /* 学习模式 */
    {9,  6, 4, 0, 0},  /* 自控模式 */
    {10, 6, 4, 0, 0},  /* 自控灯-白 */
    {11, 6, 4, 1, 0},  /* 自控灯-青 */
    {12, 6, 4, 2, 0},  /* 自控灯-黄 */
    {13, 6, 4, 3, 0},  /* 自控灯-紫 */
    {14, 6, 4, 4, 0},  /* 自控灯-蓝 */
    {15, 6, 4, 5, 0},  /* 自控灯-红 */
    {16, 6, 4, 6, 0},  /* 自控灯-绿 */
    {17, 0, 0, 0, 1},  /* 亮度加 */
    {18, 0, 0, 0, 2},  /* 亮度减 */
    {19, 0, 1, 0, 3},  /* 亮度1 */
    {20, 0, 2, 0, 4},  /* 亮度2 */
    {21, 0, 3, 0, 5},  /* 亮度3 */
    {22, 0, 4, 0, 6},  /* 亮度4 */
};
#define VOICE_TABLE_SIZE (sizeof(voice_table) / sizeof(voice_table[0]))

void voice_init(void)
{
    uart_config_t uart_cfg = {
        .baud_rate  = VOICE_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };

    ESP_ERROR_CHECK(uart_driver_install(VOICE_UART_NUM, 256, 256, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(VOICE_UART_NUM, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(VOICE_UART_NUM,
        VOICE_GPIO_TX, VOICE_GPIO_RX,
        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "ASRPRO UART2 initialized: TX=GPIO%u, RX=GPIO%u",
             VOICE_GPIO_TX, VOICE_GPIO_RX);
}

void voice_send_event(uint8_t event)
{
    uint8_t frame[4] = { FRAME_HEADER, event, FRAME_TAIL1, FRAME_TAIL2 };
    uart_write_bytes(VOICE_UART_NUM, (const char *)frame, sizeof(frame));
}

static void voice_task(void *arg)
{
    frame_state_t state = FRAME_STATE_IDLE;
    uint8_t       rx_byte;
    uint8_t       cmd_code = 0U;

    ESP_LOGI(TAG, "Voice task started.");

    while (1) {
        /* 从 UART2 读 1 字节, 100ms 超时 */
        int len = uart_read_bytes(VOICE_UART_NUM, &rx_byte, 1,
                                  pdMS_TO_TICKS(100));
        if (len <= 0) {
            continue;
        }

        switch (state) {
        case FRAME_STATE_IDLE:
            if (rx_byte == FRAME_HEADER) {
                state = FRAME_STATE_GOT_FF;
            }
            break;

        case FRAME_STATE_GOT_FF:
            cmd_code = rx_byte;
            state = FRAME_STATE_GOT_CMD;
            break;

        case FRAME_STATE_GOT_CMD:
            if (rx_byte == FRAME_TAIL1) {
                state = FRAME_STATE_GOT_FE;
            } else {
                state = FRAME_STATE_IDLE;  /* 帧错误, 重置 */
            }
            break;

        case FRAME_STATE_GOT_FE:
            if (rx_byte == FRAME_TAIL2) {
                /* 完整帧: 0xFF + code + 0xFE + 0xEE */
                ESP_LOGI(TAG, "Voice cmd: %u", cmd_code);

                /* 查表映射 */
                lamp_cmd_t cmd = { .source = CMD_SRC_VOICE };
                uint8_t matched = 0U;
                for (size_t i = 0U; i < VOICE_TABLE_SIZE; i++) {
                    if (voice_table[i].code == cmd_code) {
                        cmd.mode     = voice_table[i].mode;
                        cmd.light    = voice_table[i].light;
                        cmd.color    = voice_table[i].color;
                        cmd.cmd_type = voice_table[i].special;
                        matched = 1U;
                        break;
                    }
                }
                if (matched) {
                    xQueueSend(g_cmd_queue, &cmd, 0);
                } else {
                    ESP_LOGW(TAG, "Unknown voice code: %u", cmd_code);
                }
            }
            state = FRAME_STATE_IDLE;  /* 完成或错误, 都重置 */
            break;

        default:
            state = FRAME_STATE_IDLE;
            break;
        }
    }
}

void voice_task_init(void)
{
    voice_init();
    xTaskCreate(voice_task, "voice", 2560, NULL, 8, NULL);
}
