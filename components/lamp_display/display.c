/**
 * @file display.c
 * @brief SSD1306 128x64 OLED I2C 显示驱动 — ESP32 移植版
 *
 * 对应原 STM32 OLED.c (软件 I2C GPIO 模拟)
 * 改为 ESP32 硬件 I2C0 控制器 + ESP-IDF i2c_master API
 *
 * 和 STM32 版本的关键差异:
 *   - I2C 协议由硬件控制器处理 (原版用 GPIO 手动模拟 SCL/SDA 时序)
 *   - 不再需要 OLED_I2C_Start/Stop/SendByte 函数
 *   - 写命令/数据改为通过 i2c_master_transmit() 发送控制字节 + 数据
 *   - 用 FreeRTOS 任务而非 while(1) 阻塞循环刷新
 *
 * SSD1306 驱动芯片:
 *   - 分辨率: 128×64 像素
 *   - 8 页 (page 0-7), 每页 8 像素高
 *   - I2C 地址: 0x3C (7位) 即原 STM32 代码中的 0x78 (0x3C<<1)
 *   - 控制字节: 0x00=命令, 0x40=数据
 *
 * 引脚: GPIO21=SDA, GPIO22=SCL (I2C0)
 */

#include "display.h"
#include "font8x16.h"
#include "main.h"
#include "lamp_core.h"       /* alarm_set_state_t, g_alarm_* */
#include "alarm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include <time.h>
#include <stdlib.h>           /* setenv / tzset */
#include <string.h>           /* strlen / snprintf */

/* 全局状态, 定义在 lamp_core.c */
extern uint8_t  g_mode;
extern uint8_t  g_light_level;
extern uint8_t  g_color_index;
extern uint8_t  g_temp;
extern uint8_t  g_humi;
extern uint16_t g_study_time;
extern uint8_t  g_flag_count;

static const char *TAG = "display";

/* ====== I2C 配置常量 ====== */
#define I2C_MASTER_PORT       I2C_NUM_0
#define I2C_MASTER_SDA_IO     21U
#define I2C_MASTER_SCL_IO     22U
#define I2C_MASTER_FREQ_HZ    1000000U    /* 1MHz 快速模式+ */

/* SSD1306 I2C 地址 (7位) */
#define SSD1306_ADDR          0x3CU

/* 控制字节 */
#define SSD1306_CTRL_CMD      0x00U       /* 下一个字节是命令 */
#define SSD1306_CTRL_DATA     0x40U       /* 下一个字节是数据 */

/* 显示尺寸 */
#define SSD1306_WIDTH         128U
#define SSD1306_HEIGHT        64U
#define SSD1306_PAGES         8U          /* 64像素 / 8像素每页 */

/* 写命令的超时时间 (毫秒) */
#define I2C_TIMEOUT_MS        100

/* ====== 模式名查表 ====== */
static const char *s_mode_names[] = {
    "Normal", "Cold", "Warm", "Color", "Night", "Study", "Auto"
};
#define MODE_NAME_COUNT (sizeof(s_mode_names) / sizeof(s_mode_names[0]))

/* ====== I2C 句柄 ====== */
static i2c_master_bus_handle_t g_bus_handle = NULL;
static i2c_master_dev_handle_t g_dev_handle = NULL;

/* ====== 辅助函数 ====== */

/**
 * @brief 向 SSD1306 发送命令
 *
 * I2C 协议: [从机地址] [0x00] [命令字节]
 * 0x00 告诉 SSD1306 接下来的字节是命令而非数据
 */
static void ssd1306_write_cmd(uint8_t cmd)
{
    uint8_t buf[2] = { SSD1306_CTRL_CMD, cmd };
    ESP_ERROR_CHECK(i2c_master_transmit(g_dev_handle, buf, sizeof(buf),
                                        I2C_TIMEOUT_MS));
}

/**
 * @brief 向 SSD1306 发送数据 (单字节)
 *
 * I2C 协议: [从机地址] [0x40] [数据字节]
 * 0x40 告诉 SSD1306 接下来的字节是写入 GDRAM 的像素数据
 */
static void ssd1306_write_data(uint8_t data)
{
    uint8_t buf[2] = { SSD1306_CTRL_DATA, data };
    ESP_ERROR_CHECK(i2c_master_transmit(g_dev_handle, buf, sizeof(buf),
                                        I2C_TIMEOUT_MS));
}

/**
 * @brief 向 SSD1306 发送多字节数据
 *
 * 用于批量写入像素数据, 减少 I2C 通信次数。
 * 比如清屏时一次写 128 字节 (一整页)。
 *
 * @param data   像素数据缓冲区
 * @param len    数据长度 (最大 128, 因为一页只有 128 列)
 */
static void ssd1306_write_data_bulk(const uint8_t *data, size_t len)
{
    if (len == 0U) return;

    /* 控制字节 + 数据: 总缓冲区大小 = 1 + len */
    uint8_t buf[129];  /* 栈上分配, 1 控制字节 + 最多 128 数据字节 */
    buf[0] = SSD1306_CTRL_DATA;
    for (size_t i = 0U; i < len; i++) {
        buf[i + 1U] = data[i];
    }
    ESP_ERROR_CHECK(i2c_master_transmit(g_dev_handle, buf, len + 1U,
                                        I2C_TIMEOUT_MS));
}

/**
 * @brief 设置光标位置 (页地址 + 列地址)
 *
 * SSD1306 的坐标系统:
 *   - 页 (Y方向): 0~7, 每页 8 像素高
 *   - 列 (X方向): 0~127, 每个地址一列
 * 原 STM32 代码中 Y 是页, X 是列, 和这里一致。
 *
 * SSD1306 命令:
 *   0xB0|page  : 设置页起始地址 (0xB0~0xB7)
 *   0x10|hi    : 设置列地址高 4 位
 *   0x00|lo    : 设置列地址低 4 位
 */
static void ssd1306_set_cursor(uint8_t page, uint8_t col)
{
    ssd1306_write_cmd(0xB0U | (page & 0x07U));
    ssd1306_write_cmd(0x10U | ((col & 0xF0U) >> 4U));
    ssd1306_write_cmd(0x00U | (col & 0x0FU));
}

/* ====== 公共 API ====== */

void display_init(void)
{
    /* 1. 创建 I2C 主总线
     *    和 STM32 的区别: STM32 需要手动配置 GPIO 寄存器 + 软件模拟时序,
     *    ESP32 只需传配置结构体, 硬件自动处理 SCL/SDA */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port     = I2C_MASTER_PORT,
        .sda_io_num   = I2C_MASTER_SDA_IO,
        .scl_io_num   = I2C_MASTER_SCL_IO,
        .clk_source   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7U,    /* 滤除 <7 个时钟周期的毛刺 */
        .flags.enable_internal_pullup = 1U,  /* ESP32 内部上拉 ~45kΩ */
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &g_bus_handle));

    /* 2. 在总线上添加 SSD1306 设备 */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,  /* 7位地址 */
        .device_address  = SSD1306_ADDR,
        .scl_speed_hz    = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(g_bus_handle, &dev_cfg,
                                              &g_dev_handle));

    /* 3. SSD1306 初始化序列
     *    这段命令序列和原 STM32 OLED_Init() 完全一致,
     *    只是发送方式从软件 I2C 改为硬件 I2C */
    vTaskDelay(pdMS_TO_TICKS(100));       /* 上电等待 (原代码用忙等循环) */

    ssd1306_write_cmd(0xAEU);   /* 关闭显示 */
    ssd1306_write_cmd(0xD5U);   /* 设置时钟分频比 */
    ssd1306_write_cmd(0x80U);
    ssd1306_write_cmd(0xA8U);   /* 设置多路复用率 64 */
    ssd1306_write_cmd(0x3FU);
    ssd1306_write_cmd(0xD3U);   /* 设置显示偏移 0 */
    ssd1306_write_cmd(0x00U);
    ssd1306_write_cmd(0x40U);   /* 设置显示起始行 */
    ssd1306_write_cmd(0xA1U);   /* 左右正常 (0xA0=反置) */
    ssd1306_write_cmd(0xC8U);   /* 上下正常 (0xC0=反置) */
    ssd1306_write_cmd(0xDAU);   /* COM 引脚配置 */
    ssd1306_write_cmd(0x12U);
    ssd1306_write_cmd(0x81U);   /* 对比度 */
    ssd1306_write_cmd(0xCFU);
    ssd1306_write_cmd(0xD9U);   /* 预充电周期 */
    ssd1306_write_cmd(0xF1U);
    ssd1306_write_cmd(0xDBU);   /* VCOMH 电压 */
    ssd1306_write_cmd(0x30U);
    ssd1306_write_cmd(0xA4U);   /* 正常显示模式 (非全屏点亮) */
    ssd1306_write_cmd(0xA6U);   /* 正常显示 (非反白) */
    ssd1306_write_cmd(0x8DU);   /* 使能充电泵 */
    ssd1306_write_cmd(0x14U);
    ssd1306_write_cmd(0xAFU);   /* 开启显示 */

    ESP_LOGI(TAG, "SSD1306 initialized: I2C0, SDA=GPIO%u, SCL=GPIO%u",
             I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
}

/**
 * @brief 清屏 — 将所有像素设为 0 (熄灭)
 *
 * 原理:
 *   屏幕有 8 页 (page 0-7), 每页 128 列。
 *   逐页设置光标到 (page, 0), 然后连续写入 128 字节 0x00。
 *
 * 和 STM32 原版的区别:
 *   原版每字节单独调用一次 I2C 写入 (128×8=1024 次 I2C 帧),
 *   这里一页一次写入 128 字节 (8 次 I2C 帧), 效率高 128 倍。
 */
/**
 * @brief 清空指定逻辑行 (两页, 16像素高)
 *
 * 和 display_clear() 的区别: 只清指定行, 不动其他行。
 * 用于闹钟设置界面: Line1 "SET ALARM" 始终不变, 只需清 Line2+4。
 *
 * @param line  逻辑行号 1~4
 */
static void display_clear_line(uint8_t line)
{
    static const uint8_t zeros[SSD1306_WIDTH] = { 0U };
    uint8_t start_page = (line - 1U) * 2U;          /* 上半页 */
    ssd1306_set_cursor(start_page, 0U);
    ssd1306_write_data_bulk(zeros, SSD1306_WIDTH);
    ssd1306_set_cursor((uint8_t)(start_page + 1U), 0U); /* 下半页 */
    ssd1306_write_data_bulk(zeros, SSD1306_WIDTH);
}

/**
 * @brief 清空指定行的一段列区间 (上下两页)
 *
 * @param line       逻辑行号 1~4
 * @param start_col  起始列 1~16
 * @param end_col    结束列 1~16 (含)
 */
static void display_clear_cols(uint8_t line, uint8_t start_col, uint8_t end_col)
{
    if (start_col < 1U || end_col > 16U || start_col > end_col) return;
    static const uint8_t zeros[SSD1306_WIDTH] = { 0U };
    uint8_t start_page  = (line - 1U) * 2U;
    uint8_t pixel_start = (start_col - 1U) * 8U;
    uint8_t pixel_count = (end_col - start_col + 1U) * 8U;

    ssd1306_set_cursor(start_page, pixel_start);
    ssd1306_write_data_bulk(zeros, pixel_count);
    ssd1306_set_cursor((uint8_t)(start_page + 1U), pixel_start);
    ssd1306_write_data_bulk(zeros, pixel_count);
}

static void display_clear(void)
{
    static const uint8_t zeros[SSD1306_WIDTH] = { 0U };
    /* 静态空数组, 编译器放入 ROM */

    for (uint8_t page = 0U; page < SSD1306_PAGES; page++) {
        ssd1306_set_cursor(page, 0U);
        ssd1306_write_data_bulk(zeros, SSD1306_WIDTH);
    }
}

/**
 * @brief 显示一个 8×16 像素的 ASCII 字符
 *
 * 字体来源: OLED_F8x16 (原 STM32 项目的 font8x16.h)
 *   每个字符占用 16 字节: 上 8 字节 + 下 8 字节
 *   第 0 个字符是空格, 所以用 Char - ' ' 作索引
 *
 * 绘制过程:
 *   1. 设置光标到上半页 (page = (Line-1)*2)
 *   2. 写 8 字节上半部分像素
 *   3. 设置光标到下半页 (page = (Line-1)*2 + 1)
 *   4. 写 8 字节下半部分像素
 *
 * @param line   行号 1~4
 * @param col    列号 1~16
 * @param ch     ASCII 可打印字符
 */
static void display_show_char(uint8_t line, uint8_t col, char ch)
{
    if (line < 1U || line > 4U || col < 1U || col > 16U) return;
    if (ch < ' ' || ch > '~') {
        ch = ' ';  /* 不可打印字符显示空格 */
    }

    uint8_t index = (uint8_t)(ch - ' ');
    uint8_t upper_page = (line - 1U) * 2U;
    uint8_t lower_page = upper_page + 1U;
    uint8_t pixel_col  = (col - 1U) * 8U;

    /* 上半部分 (8 像素高) */
    ssd1306_set_cursor(upper_page, pixel_col);
    for (uint8_t i = 0U; i < 8U; i++) {
        ssd1306_write_data(OLED_F8x16[index][i]);
    }

    /* 下半部分 (8 像素高) */
    ssd1306_set_cursor(lower_page, pixel_col);
    for (uint8_t i = 0U; i < 8U; i++) {
        ssd1306_write_data(OLED_F8x16[index][i + 8U]);
    }
}

/**
 * @brief 显示字符串
 *
 * @param line   起始行 1~4
 * @param col    起始列 1~16
 * @param str    以 '\0' 结尾的字符串
 */
static void display_show_string(uint8_t line, uint8_t col, const char *str)
{
    if (str == NULL) return;

    uint8_t i = 0U;
    while (str[i] != '\0' && (col + i) <= 16U) {
        display_show_char(line, (uint8_t)(col + i), str[i]);
        i++;
    }
}

/**
 * @brief 计算 X 的 Y 次方 (整数)
 *
 * 从原 STM32 OLED_Pow 移植, 用于 OLED_ShowNum 的逐位提取。
 * 用简单的乘法循环, 不需要浮点运算。
 */
static uint32_t int_pow(uint32_t base, uint32_t exp)
{
    uint32_t result = 1UL;
    while (exp > 0U) {
        result *= base;
        exp--;
    }
    return result;
}

/**
 * @brief 显示十进制无符号数字
 *
 * 逐位提取: Number / 10^(Length-i-1) % 10, 转换为 ASCII 数字
 *
 * @param line     行号 1~4
 * @param col      起始列 1~16
 * @param number   要显示的数字 0~4294967295
 * @param length   数字位数 1~10
 */
static void display_show_num(uint8_t line, uint8_t col, uint32_t number,
                             uint8_t length)
{
    if (length < 1U || length > 10U) return;

    for (uint8_t i = 0U; i < length; i++) {
        uint32_t digit = (number / int_pow(10UL, (uint32_t)(length - i - 1U)))
                        % 10UL;
        display_show_char(line, (uint8_t)(col + i), (char)('0' + digit));
    }
}

/**
 * @brief 在 16 字符行上居中显示字符串
 */
static void display_show_centered(uint8_t line, const char *str)
{
    if (str == NULL) return;
    size_t len = strlen(str);
    if (len > 16U) len = 16U;
    uint8_t col = (uint8_t)((16U - len) / 2U + 1U);
    display_show_string(line, col, str);
}

void display_update(const oled_data_t *data)
{
    /* 已改为直接读全局变量 + 系统时间, 保留此接口供后续使用 */
    (void)data;
}

/* ====== OLED 刷新任务 ====== */

/**
 * @brief 闹钟设置界面 — 显示 HH:MM:SS 并闪烁当前字段
 *
 * 每 500ms 调用一次, toggle 在奇偶帧之间切换:
 *   偶数帧: 显示全部数字
 *   奇数帧: 当前字段显示空格 (实现闪烁效果)
 *
 * 布局:
 *   Line 1: "SET ALARM"
 *   Line 2: "  HH:MM:SS"  (2-7列)
 *   Line 4: "M:+  A:Next 10s"
 */
static void display_alarm_setting(void)
{
    static uint8_t toggle = 0U;   /* 0=显示, 1=隐藏(闪烁) */

    /* 只清 Line2 (HH:MM:SS 闪烁) + Line4 (提示切换),
     * Line1 "SET ALARM" 已在切页时全清 + 首帧绘制, 之后不动 */
    display_clear_line(2U);
    display_clear_line(4U);

    /* Line 1: 标题 (全清时已绘制, 后续帧不重绘) */
    display_show_string(1U, 1U, "SET ALARM");

    /* Line 2: HH:MM:SS (每个数字 16 像素高, 无需重复画两行) */
    /* 小时: 列2-3 */
    if (toggle == 0U || g_alarm_state != ALARM_SET_HOUR) {
        display_show_num(2U, 2U, (uint32_t)g_alarm_hour, 2U);
    }
    /* 分隔符 : */
    display_show_char(2U, 4U, ':');

    /* 分钟: 列5-6 */
    if (toggle == 0U || g_alarm_state != ALARM_SET_MIN) {
        display_show_num(2U, 5U, (uint32_t)g_alarm_min, 2U);
    }
    /* 分隔符 : */
    display_show_char(2U, 7U, ':');

    /* 秒: 列8-9 */
    if (toggle == 0U || g_alarm_state != ALARM_SET_SEC) {
        display_show_num(2U, 8U, (uint32_t)g_alarm_sec, 2U);
    }

    /* Line 4: 操作提示 (根据当前阶段) */
    if (g_alarm_state == ALARM_SET_CONFIRM) {
        display_show_string(4U, 1U, "M:Quit A:OK");
    } else {
        display_show_string(4U, 1U, "M:+  A:Next");
    }

    toggle = (toggle == 0U) ? 1U : 0U;
}
/**
 * @brief OLED 显示刷新任务 (1s 周期, 优先级 5)
 *
 * 脏检测优化:
 *   缓存上次显示的所有值, 仅在数据真正变化时才 clear + 重绘全屏。
 *   典型场景:
 *     - 无闹钟/无传感器变化: 仅每分钟 (时钟分钟跳变) 重绘一次
 *     - 闹钟激活: 每秒 (倒计时秒变化) 重绘
 *     - 传感器/模式/按键变化: 变化时立即重绘
 *   相比原先每秒无条件全刷, CPU 和 I2C 总线占用大幅降低。
 */
static void oled_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "OLED task started.");

    /* 缓存: 初始化为不可能值, 确保首帧全刷 */
    static uint8_t  last_hour       = 0xFFU;
    static uint8_t  last_min        = 0xFFU;
    static uint8_t  last_humi       = 0xFFU;
    static uint8_t  last_temp       = 0xFFU;
    static uint8_t  last_status     = 0xFFU;  /* '-','W','B','&' */
    static uint8_t  last_mode       = 0xFFU;
    static uint8_t  last_level      = 0xFFU;
    static uint8_t  last_color      = 0xFFU;
    static uint16_t last_study      = 0xFFFFU;
    static uint32_t last_alarm_rest = 0xFFFFFFFFUL;
    static uint8_t  last_flag       = 0xFFU;

    static uint8_t alarm_need_full_clear = 1U;

    while (1) {
        /* ====== 闹钟设置模式 ====== */
        if (g_alarm_state != ALARM_SET_IDLE) {
            if (alarm_need_full_clear) {
                display_clear();
                alarm_need_full_clear = 0U;
            }
            display_alarm_setting();
            /* 退出后强制全刷 */
            last_hour = last_min = last_humi = last_temp = 0xFFU;
            last_status = 0xFFU;
            last_mode = last_level = last_color = 0xFFU;
            last_study = 0xFFFFU;
            last_alarm_rest = 0xFFFFFFFFUL;
            last_flag = 0xFFU;
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        alarm_need_full_clear = 1U;

        /* ====== 读取当前值 ====== */
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        uint8_t hour = (uint8_t)timeinfo.tm_hour;
        uint8_t min  = (uint8_t)timeinfo.tm_min;
        uint8_t humi = g_humi;
        uint8_t temp = g_temp;
        uint8_t mode = g_mode;
        uint8_t level = g_light_level;
        uint8_t color = g_color_index;
        uint16_t study = g_study_time;
        uint32_t alarm_rest = alarm_get_remaining();
        uint8_t flag = g_flag_count;

        EventBits_t bits = xEventGroupGetBits(g_system_events);
        uint8_t wifi_ok = (bits & EVT_WIFI_CONNECTED) ? 1U : 0U;
        uint8_t ble_ok  = (bits & EVT_BLE_CONNECTED)  ? 1U : 0U;
        char status_ch = (wifi_ok && ble_ok) ? '&' : (wifi_ok ? 'W' : (ble_ok ? 'B' : '-'));

        /* ====== 首帧: 全清全刷 ====== */
        if (last_hour == 0xFFU) {
            display_clear();
            goto DRAW_ALL;
        }

        /* ====== 差分刷新 ====== */

        /* Line 1: 时间 (col 1-5) */
        if (hour != last_hour || min != last_min) {
            display_clear_cols(1U, 1U, 5U);
            display_show_num(1U, 1U, hour, 2U);
            display_show_char(1U, 3U, ':');
            display_show_num(1U, 4U, min, 2U);
            last_hour = hour;
            last_min  = min;
        }

        /* Line 1: 湿度 (col 8-10) — 与首帧全刷布局一致 */
        if (humi != last_humi) {
            display_clear_cols(1U, 8U, 10U);
            display_show_num(1U, 8U, humi, 2U);
            display_show_string(1U, 10U, "%");
            last_humi = humi;
        }

        /* Line 1: 温度 (col 12-14) — 与首帧全刷布局一致 */
        if (temp != last_temp) {
            display_clear_cols(1U, 12U, 14U);
            display_show_num(1U, 12U, temp, 2U);
            display_show_string(1U, 14U, "C");
            last_temp = temp;
        }

        /* Line 1: 连接状态 (col 16) */
        if ((uint8_t)status_ch != last_status) {
            display_clear_cols(1U, 16U, 16U);
            display_show_char(1U, 16U, status_ch);
            last_status = (uint8_t)status_ch;
        }

        /* Line 2: 模式名 (整行) */
        if (mode != last_mode) {
            display_clear_line(2U);
            if (mode < MODE_NAME_COUNT) {
                display_show_centered(2U, s_mode_names[mode]);
            }
        }

        /* Line 3: 模式参数 (整行, 依赖 mode/level/color/study) */
        {
            uint8_t line3_dirty = 0U;
            if (mode != last_mode)                                          line3_dirty = 1U;
            if (level != last_level && (mode == LAMP_MODE_NORMAL
                || mode == LAMP_MODE_COLD || mode == LAMP_MODE_WARM
                || mode == LAMP_MODE_STUDY))                                line3_dirty = 1U;
            if (color != last_color && (mode == LAMP_MODE_COLOR
                || mode == LAMP_MODE_AUTO))                                 line3_dirty = 1U;
            if (study != last_study && mode == LAMP_MODE_STUDY)             line3_dirty = 1U;

            if (line3_dirty) {
                display_clear_line(3U);
                switch (mode) {
                case LAMP_MODE_NORMAL:
                case LAMP_MODE_COLD:
                case LAMP_MODE_WARM:
                    display_show_centered(3U, "Level: ");
                    display_show_num(3U, 12U, level, 1U);
                    break;
                case LAMP_MODE_COLOR:
                    display_show_centered(3U, "Color: ");
                    display_show_num(3U, 12U, color, 2U);
                    break;
                case LAMP_MODE_NIGHT:
                    display_show_centered(3U, "--");
                    break;
                case LAMP_MODE_STUDY: {
                    char buf[11];
                    uint8_t sh = (uint8_t)(study / 60U);
                    uint8_t sm = (uint8_t)(study % 60U);
                    snprintf(buf, sizeof(buf), "Stu:%02u:%02u", sh, sm);
                    display_show_centered(3U, buf);
                    break;
                }
                case LAMP_MODE_AUTO:
                    display_show_centered(3U, "Color: ");
                    display_show_num(3U, 12U, color, 1U);
                    break;
                default: break;
                }
            }
        }

        /* Line 4: 闹钟 (整行) */
        if (alarm_rest != last_alarm_rest || flag != last_flag) {
            display_clear_line(4U);
            if (flag != 0U) {
                char abuf[20];
                snprintf(abuf, sizeof(abuf), "A %02u:%02u:%02u",
                         (unsigned)(alarm_rest / 3600U),
                         (unsigned)((alarm_rest % 3600U) / 60U),
                         (unsigned)(alarm_rest % 60U));
                display_show_centered(4U, abuf);
            } else {
                display_show_centered(4U, "A --:--:--");
            }
            last_alarm_rest = alarm_rest;
            last_flag = flag;
        }

        /* 更新非行级缓存 */
        last_mode  = mode;
        last_level = level;
        last_color = color;
        last_study = study;

        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;

DRAW_ALL:
        /* 首帧全刷: Line 1 */
        display_show_num(1U, 1U, hour, 2U);
        display_show_char(1U, 3U, ':');
        display_show_num(1U, 4U, min, 2U);
        display_show_num(1U, 8U, humi, 2U);
        display_show_string(1U, 10U, "%");
        display_show_num(1U, 12U, temp, 2U);
        display_show_string(1U, 14U, "C");
        display_show_char(1U, 16U, status_ch);

        /* Line 2 */
        if (mode < MODE_NAME_COUNT) {
            display_show_centered(2U, s_mode_names[mode]);
        }

        /* Line 3 */
        switch (mode) {
        case LAMP_MODE_NORMAL:
        case LAMP_MODE_COLD:
        case LAMP_MODE_WARM:
            display_show_centered(3U, "Level: ");
            display_show_num(3U, 12U, level, 1U);
            break;
        case LAMP_MODE_COLOR:
            display_show_centered(3U, "Color: ");
            display_show_num(3U, 12U, color, 2U);
            break;
        case LAMP_MODE_NIGHT:
            display_show_centered(3U, "--");
            break;
        case LAMP_MODE_STUDY: {
            char buf[11];
            uint8_t sh = (uint8_t)(study / 60U);
            uint8_t sm = (uint8_t)(study % 60U);
            snprintf(buf, sizeof(buf), "Stu:%02u:%02u", sh, sm);
            display_show_centered(3U, buf);
            break;
        }
        case LAMP_MODE_AUTO:
            display_show_centered(3U, "Color: ");
            display_show_num(3U, 12U, color, 1U);
            break;
        default: break;
        }

        /* Line 4 */
        if (flag != 0U) {
            char abuf[20];
            snprintf(abuf, sizeof(abuf), "A %02u:%02u:%02u",
                     (unsigned)(alarm_rest / 3600U),
                     (unsigned)((alarm_rest % 3600U) / 60U),
                     (unsigned)(alarm_rest % 60U));
            display_show_centered(4U, abuf);
        } else {
            display_show_centered(4U, "A --:--:--");
        }

        /* 保存首帧缓存 */
        last_hour = hour; last_min = min;
        last_humi = humi; last_temp = temp;
        last_status = (uint8_t)status_ch;
        last_mode = mode; last_level = level; last_color = color;
        last_study = study;
        last_alarm_rest = alarm_rest; last_flag = flag;

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void oled_task_init(void)
{
    /* 时区设为北京时间 (UTC+8), 必须在 localtime_r 调用之前设定 */
    setenv("TZ", "CST-8", 1);
    tzset();

    display_init();
    display_clear();

    /* 初始化时显示启动画面 */
    display_show_string(2U, 1U, "  SmartLamp");
    display_show_string(3U, 1U, "  ESP32 Ready");
    vTaskDelay(pdMS_TO_TICKS(1000));

    xTaskCreate(oled_task, "oled", 2560, NULL, 5, NULL);
}
