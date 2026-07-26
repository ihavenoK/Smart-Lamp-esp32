/**
 * @file sensor.c
 * @brief 传感器组件 — DHT11 (GPIO位操作) + ADC + LD2402 毫米波 + 按键扫描
 *
 * 对应原 STM32: DHT11.c + AD.c + IrDA.c + (部分 Key.c)
 *
 * DHT11 驱动说明:
 *   espressif/dht 组件仓库 403 不可用, 改用手写 GPIO 位操作实现。
 *   时序完全参照原 STM32 dht11.c:
 *     复位: 拉低 20ms → 拉高 30us → 切换到输入
 *     应答: 等待 DHT11 拉低 40-80us → 等待拉高 40-80us
 *     读取: 40 位 (5 字节), 校验和验证
 *   esp_rom_delay_us() 提供微秒级忙等延时, 和 STM32 delay_us() 等效。
 *   完整读取阻塞约 25ms, 传感器任务周期 2s, 不影响调度。
 */

#include "sensor.h"
#include "key.h"
#include "radar.h"
#include "main.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_rom_sys.h"         /* esp_rom_delay_us() — 微秒忙等延时 */
#include "esp_log.h"

static const char *TAG = "sensor";

/* DHT11 临界区保护: 40us 级时序会被 WiFi/调度器中断打乱 */
static portMUX_TYPE g_dht11_mux = portMUX_INITIALIZER_UNLOCKED;

static adc_oneshot_unit_handle_t g_adc_handle = NULL;
static adc_cali_handle_t         g_cali_handle = NULL;

/* ====== DHT11 GPIO 位操作驱动 (替代 espressif/dht 组件) ====== */

/**
 * @brief 从 DHT11 读取 1 位数据
 *
 * 协议时序:
 *   每个 bit 以 50us 低电平开始, 然后高电平:
 *     bit 0: 26-28us 高电平
 *     bit 1: 70us   高电平
 *   在低电平→高电平跳变后等 40us 再采样:
 *     仍为高 → bit 1 (70us > 40us)
 *     已变低 → bit 0 (26-28us < 40us)
 *
 * 和原 STM32 DHT11_Read_Bit() 逻辑完全一致。
 */
static uint8_t dht11_read_bit(void)
{
    uint16_t retry;

    /* 等待 DHT11 拉低 (bit 起始信号) */
    retry = 0U;
    while (gpio_get_level(DHT11_GPIO) == 1U) {
        if (++retry > 100U) return 0U;   /* 超时 */
        esp_rom_delay_us(1U);
    }

    /* 等待 DHT11 拉高 (bit 数据段开始) */
    retry = 0U;
    while (gpio_get_level(DHT11_GPIO) == 0U) {
        if (++retry > 100U) return 0U;
        esp_rom_delay_us(1U);
    }

    /* 延迟 40us 后采样: 高=1, 低=0 */
    esp_rom_delay_us(40U);
    return (gpio_get_level(DHT11_GPIO) == 1U) ? 1U : 0U;
}

/**
 * @brief 从 DHT11 读取一次完整数据 (5 字节)
 *
 * 协议流程 (对应原 STM32 DHT11_Read_Data):
 *   1. 复位脉冲: 拉低 20ms → 拉高 30us
 *   2. 切换为输入, 等待 DHT11 应答
 *   3. 读取 40 位 (5 字节): humi_int, humi_dec, temp_int, temp_dec, checksum
 *   4. 校验和验证: (buf[0]+buf[1]+buf[2]+buf[3]) & 0xFF == buf[4]
 *
 * @param humi   [out] 湿度整数部分 (20-90%)
 * @param temp   [out] 温度整数部分 (0-50°C)
 * @return 0 成功, 1 无应答, 2 校验失败
 */
static uint8_t dht11_read(uint8_t *humi, uint8_t *temp)
{
    uint8_t  buf[5] = { 0U, 0U, 0U, 0U, 0U };
    uint16_t retry;
    uint8_t  i, j;

    /* 1. 复位脉冲: 拉低 20ms (用 vTaskDelay 放 CPU, 非忙等) */
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT11_GPIO, 0U);
    vTaskDelay(pdMS_TO_TICKS(20));      /* 20ms 放 CPU 给其他任务 */
    gpio_set_level(DHT11_GPIO, 1U);
    esp_rom_delay_us(30U);              /* 30us (协议要求 20-40us) */

    /* 2. 切换到输入 */
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_INPUT);

    /* ====== 临界区: 应答 + 40位读取 + 校验 (约 5ms)
     *   禁止中断, 避免 WiFi/调度器打乱微秒级时序 ====== */
    portENTER_CRITICAL(&g_dht11_mux);

    /* DHT11 拉低 40-80us 表示应答 */
    retry = 0U;
    while (gpio_get_level(DHT11_GPIO) == 1U) {
        if (++retry > 100U) {
            portEXIT_CRITICAL(&g_dht11_mux);
            return 1U;                  /* 超时: DHT11 不存在/断线 */
        }
        esp_rom_delay_us(1U);
    }

    /* DHT11 拉高 40-80us 表示准备就绪 */
    retry = 0U;
    while (gpio_get_level(DHT11_GPIO) == 0U) {
        if (++retry > 100U) {
            portEXIT_CRITICAL(&g_dht11_mux);
            return 1U;
        }
        esp_rom_delay_us(1U);
    }

    /* 3. 读取 5 字节 (40 位) */
    for (i = 0U; i < 5U; i++) {
        buf[i] = 0U;
        for (j = 0U; j < 8U; j++) {
            buf[i] <<= 1U;
            buf[i] |= dht11_read_bit();
        }
    }

    portEXIT_CRITICAL(&g_dht11_mux);
    /* ====== 临界区结束 ====== */

    /* 4. 校验和: 前 4 字节和的最低 8 位应等于第 5 字节 */
    if ((uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]) != buf[4]) {
        return 2U;                      /* 校验失败 */
    }

    /* DHT11 小数部分固定为 0, 仅取整数部分 */
    *humi = buf[0];                     /* 湿度 20-90% */
    *temp = buf[2];                     /* 温度 0-50°C */
    return 0U;
}

/* ====== 初始化 ====== */

void sensor_init(void)
{
    /* LD2402 毫米波人体存在传感器 — GPIO27 输入
     * 官方说明 GPIO 模式即插即用, RX 悬空即可。
     * 使能内部弱下拉 (~45kΩ): LD2402 OUT 是推挽输出不受影响,
     * 但浮空/接触不良时默认为低(无人), 避免误触发亮灯 */
    gpio_config_t ir_conf = {
        .pin_bit_mask = (1ULL << IR_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,   /* 弱下拉, 浮空时默认 LOW */
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&ir_conf);

    /* DHT11 — GPIO26
     * 配置为双向 (INPUT_OUTPUT), 读取时切换方向。
     * 不使能内部上下拉: DHT11 模块自带 4.7-10k 上拉电阻。 */
    gpio_config_t dht11_conf = {
        .pin_bit_mask = (1ULL << DHT11_GPIO),
        .mode         = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&dht11_conf);
    gpio_set_level(DHT11_GPIO, 1U);     /* 空闲高电平 */

    /* ADC1 — GPIO34 (ADC1_CH6), eFuse校准 */
    adc_oneshot_unit_init_cfg_t adc_init = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_init, &g_adc_handle));

    adc_oneshot_chan_cfg_t adc_chan = {
        .atten    = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(g_adc_handle, ADC_CHANNEL, &adc_chan));

    /* eFuse 校准 (v6.0: curve_fitting 改名为 line_fitting) */
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .atten    = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_line_fitting(&cali_cfg, &g_cali_handle));

    /* 按键 (50ms 定时器扫描, 非阻塞) */
    key_init();

    /* LD2402 毫米波雷达 UART1 驱动 */
    radar_init();

    ESP_LOGI(TAG, "Sensors ready: DHT11=GPIO%u IR=GPIO%u ADC=GPIO%u Radar=UART1",
             DHT11_GPIO, IR_GPIO, ADC_GPIO);
}

/* ====== 传感器采集任务 ====== */

static void sensor_task(void *arg)
{
    sensor_data_t data;

    /* DHT11 失败时保持上次有效值, 避免 OLED 跳变到 0 */
    static uint8_t cached_humi = 0U;
    static uint8_t cached_temp = 0U;

    ESP_LOGI(TAG, "Sensor task started.");

    while (1) {
        /* 1. ADC 读取 (光敏电阻) */
        int adc_raw = 0;
        int voltage_mv = 0;
        adc_oneshot_read(g_adc_handle, ADC_CHANNEL, &adc_raw);
        adc_cali_raw_to_voltage(g_cali_handle, adc_raw, &voltage_mv);
        data.adc_val = (uint16_t)adc_raw;

        /* 2. 红外传感器读取 */
        data.ir_state = (uint8_t)gpio_get_level(IR_GPIO);

        /* 3. DHT11 温湿度读取 (GPIO 位操作, 阻塞约 25ms) */
        uint8_t humi = 0U, temp = 0U;
        if (dht11_read(&humi, &temp) == 0U) {
            data.humi     = humi;
            data.temp     = temp;
            cached_humi   = humi;
            cached_temp   = temp;
        } else {
            /* 读取失败 (偶发, DHT11 对时序敏感): 复用上次有效值 */
            data.humi = cached_humi;
            data.temp = cached_temp;
            ESP_LOGW(TAG, "DHT11 read failed, reusing cached values");
        }

        /* 4. LD2402 毫米波雷达 — UART 距离+状态 (雷达任务自动刷新) */
        data.radar_dist     = radar_get_distance();
        data.radar_presence = radar_get_presence();

        /* 5. 推送到传感器队列 (非阻塞, 队列满则丢弃最旧数据) */
        xQueueSend(g_sensor_queue, &data, 0);

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void sensor_task_init(void)
{
    sensor_init();
    radar_task_init();   /* LD2402 雷达读取任务 (优先级 3) */
    xTaskCreate(sensor_task, "sensor", 3072, NULL, 6, NULL);
}
