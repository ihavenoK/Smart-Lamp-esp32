/**
 * @file display.c
 * @brief SSD1306 128x64 OLED + LVGL 9 显示组件 (v1.0.2 显示优化)
 *
 * 显示优化:
 *   - 四行紧排布局 (按字形实际高度排布, 不裁切不留白)
 *   - 状态由 W/B 字母改为 WiFi/BLE/闹钟小图标
 *   - 温度单位显示为 °C (使用字库自带 U+00B0 度数符号)
 *   - 大字时钟右侧增加秒显示
 *   - I1 亮度阈值 95 (修复断笔画与毛糙)
 */

#include "display.h"
#include "main.h"
#include "lamp_core.h"
#include "alarm.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "driver/i2c_master.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "display";

/* ====== 硬件配置 (与 1.0.1 一致) ====== */
#define I2C_MASTER_PORT       I2C_NUM_0
#define I2C_MASTER_SDA_IO     21U
#define I2C_MASTER_SCL_IO     22U
#define I2C_MASTER_FREQ_HZ    1000000U
#define SSD1306_ADDR          0x3CU
#define LCD_H_RES             128U
#define LCD_V_RES             64U

/* ====== 四行布局 y 坐标 (按字形高度紧排, 行框可叠字形不叠) ====== */
#define ROW_TOP_Y    0U
#define ROW_CLOCK_Y  11U
#define ROW_SEC_Y    19U
#define ROW_MODE_Y   32U
#define ROW_ALARM_Y  45U
#define ICON_Y       2U

/* ====== 状态图标 (12x12 RGB565 白色位图) ====== */
#define ICON_W 12U
#define ICON_H 12U
#define P_ON   0xFFFFU
#define P_OFF  0x0000U

/* WiFi: 三段弧 + 圆点 */
static const uint16_t s_icon_wifi[ICON_W * ICON_H] = {
    P_OFF, P_OFF, P_ON,  P_ON,  P_ON,  P_ON,  P_ON,  P_ON,  P_ON,  P_ON,  P_OFF, P_OFF,
    P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_ON,  P_OFF,
    P_ON,  P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_ON,
    P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF,
    P_OFF, P_OFF, P_OFF, P_ON,  P_ON,  P_ON,  P_ON,  P_ON,  P_ON,  P_OFF, P_OFF, P_OFF,
    P_OFF, P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_ON,  P_OFF, P_OFF,
    P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_ON,  P_OFF,
    P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF,
    P_OFF, P_OFF, P_OFF, P_OFF, P_ON,  P_ON,  P_ON,  P_ON,  P_OFF, P_OFF, P_OFF, P_OFF,
    P_OFF, P_OFF, P_OFF, P_OFF, P_ON,  P_ON,  P_ON,  P_ON,  P_OFF, P_OFF, P_OFF, P_OFF,
    P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF,
    P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF,
};

/* BLE: 折线 */
static const uint16_t s_icon_ble[ICON_W * ICON_H] = {
    P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF,
    P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_ON,  P_ON,  P_OFF, P_OFF, P_OFF, P_OFF, P_OFF,
    P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_ON,  P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_OFF,
    P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_ON,  P_OFF, P_OFF,
    P_OFF, P_OFF, P_ON,  P_OFF, P_ON,  P_ON,  P_OFF, P_OFF, P_ON,  P_OFF, P_OFF, P_OFF,
    P_OFF, P_OFF, P_OFF, P_ON,  P_OFF, P_ON,  P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_OFF,
    P_OFF, P_OFF, P_ON,  P_OFF, P_ON,  P_ON,  P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF,
    P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_ON,  P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_OFF,
    P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_ON,  P_OFF, P_OFF, P_ON,  P_OFF, P_OFF, P_OFF,
    P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_ON,  P_OFF, P_OFF,
    P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_ON,  P_OFF, P_OFF, P_ON,  P_OFF, P_OFF, P_OFF,
    P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_ON,  P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_OFF,
};

/* 闹钟图标(闹钟样式) */
static const uint16_t s_icon_bell[ICON_W * ICON_H] = {
    P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_ON,  P_OFF,
    P_ON,  P_ON,  P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_ON,  P_ON,
    P_OFF, P_ON,  P_ON,  P_ON,  P_ON,  P_ON,  P_ON,  P_ON,  P_ON,  P_ON,  P_ON,  P_OFF,
    P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_ON,  P_OFF,
    P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_OFF, P_ON,  P_OFF,
    P_OFF, P_ON,  P_OFF, P_OFF, P_ON,  P_ON,  P_ON,  P_ON,  P_OFF, P_OFF, P_ON,  P_OFF,
    P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_OFF, P_ON,  P_OFF,
    P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_ON,  P_OFF,
    P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_OFF, P_ON,  P_OFF,
    P_OFF, P_ON,  P_ON,  P_ON,  P_ON,  P_ON,  P_ON,  P_ON,  P_ON,  P_ON,  P_ON,  P_OFF,
    P_OFF, P_OFF, P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_OFF, P_ON,  P_OFF, P_OFF, P_OFF,
    P_OFF, P_OFF, P_OFF, P_ON,  P_OFF, P_OFF, P_OFF, P_OFF, P_ON,  P_OFF, P_OFF, P_OFF,
};

static const lv_image_dsc_t s_icon_wifi_dsc = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = ICON_W,
        .h = ICON_H,
        .stride = ICON_W * 2U,
    },
    .data_size = ICON_W * ICON_H * 2U,
    .data = (const void *)s_icon_wifi,
};

static const lv_image_dsc_t s_icon_ble_dsc = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = ICON_W,
        .h = ICON_H,
        .stride = ICON_W * 2U,
    },
    .data_size = ICON_W * ICON_H * 2U,
    .data = (const void *)s_icon_ble,
};

static const lv_image_dsc_t s_icon_bell_dsc = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = ICON_W,
        .h = ICON_H,
        .stride = ICON_W * 2U,
    },
    .data_size = ICON_W * ICON_H * 2U,
    .data = (const void *)s_icon_bell,
};

/* ====== 全局状态 (lamp_core.c) ====== */
extern uint8_t  g_mode;
extern uint8_t  g_light_level;
extern uint8_t  g_color_index;
extern uint8_t  g_temp;
extern uint8_t  g_humi;
extern uint16_t g_study_time;
extern uint8_t  g_flag_count;

/* ====== 模式名 ====== */
static const char *s_mode_names[] = {
    "Normal", "Cold", "Warm", "Color", "Night", "Study", "Auto"
};
#define MODE_NAME_COUNT (sizeof(s_mode_names) / sizeof(s_mode_names[0]))

/* ====== LVGL 控件句柄 ====== */
static lv_disp_t   *s_disp = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;   /* 供 suspend/resume 开关显示 */

/* 主界面 */
static lv_obj_t *s_scr_main  = NULL;
static lv_obj_t *s_lbl_top;     /* 温度 + 湿度 */
static lv_obj_t *s_img_wifi;   /* WiFi 状态图标 */
static lv_obj_t *s_img_ble;    /* BLE 状态图标 */
static lv_obj_t *s_img_bell;   /* 闹钟计时图标 */
static lv_obj_t *s_lbl_clock_hh;  /* 时钟 时 */
static lv_obj_t *s_lbl_clock_mm;  /* 时钟 分 */
static lv_obj_t *s_lbl_clock_ss;  /* 时钟 秒 */
static lv_obj_t *s_lbl_mode;    /* 模式 + 参数 */
static lv_obj_t *s_lbl_alarm;   /* 闹钟倒计时 */

/* 闹钟设置界面 */
static lv_obj_t *s_scr_alarm = NULL;
static lv_obj_t *s_lbl_alarm_hh;
static lv_obj_t *s_lbl_alarm_mm;
static lv_obj_t *s_lbl_alarm_ss;
static lv_obj_t *s_lbl_alarm_hint;

static uint8_t s_blink = 0U;    /* 0=显示全部字段, 1=隐藏当前编辑字段 */

/* ====== UI 辅助函数 ====== */

static lv_obj_t *ui_label_create(lv_obj_t *parent, const lv_font_t *font,
                                 lv_align_t align, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_align(lbl, align, x, y);
    return lbl;
}

static lv_obj_t *ui_icon_create(lv_obj_t *parent, const lv_image_dsc_t *dsc,
                                lv_align_t align, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, dsc);
    lv_obj_align(img, align, x, y);
    return img;
}

/* ====== 主界面创建 (四行紧排) ====== */
static void ui_create_main(void)
{
    s_scr_main = lv_screen_active();
    lv_obj_set_style_bg_color(s_scr_main, lv_color_black(), 0);

    /* 顶部行: 温度/湿度 (左侧) + 状态图标 (右侧) */
    s_lbl_top    = ui_label_create(s_scr_main, &lv_font_montserrat_12,
                                   LV_ALIGN_TOP_LEFT, 2, ROW_TOP_Y);
    s_img_wifi  = ui_icon_create(s_scr_main, &s_icon_wifi_dsc,
                                  LV_ALIGN_TOP_RIGHT, -2, ICON_Y);
    s_img_ble   = ui_icon_create(s_scr_main, &s_icon_ble_dsc,
                                  LV_ALIGN_TOP_RIGHT, -16, ICON_Y);

    /* 大字时钟 HH:MM:SS (固定位置, 数字变化不移位) */
    s_lbl_clock_hh = ui_label_create(s_scr_main, &lv_font_montserrat_24,
                                    LV_ALIGN_TOP_LEFT, 14, ROW_CLOCK_Y);
    lv_obj_t *clk_c1 = ui_label_create(s_scr_main, &lv_font_montserrat_24,
                                       LV_ALIGN_TOP_LEFT, 45, ROW_CLOCK_Y);
    lv_label_set_text(clk_c1, ":");
    s_lbl_clock_mm = ui_label_create(s_scr_main, &lv_font_montserrat_24,
                                    LV_ALIGN_TOP_LEFT, 50, ROW_CLOCK_Y);
    lv_obj_t *clk_c2 = ui_label_create(s_scr_main, &lv_font_montserrat_24,
                                       LV_ALIGN_TOP_LEFT, 81, ROW_CLOCK_Y);
    lv_label_set_text(clk_c2, ":");
    s_lbl_clock_ss = ui_label_create(s_scr_main, &lv_font_montserrat_24,
                                    LV_ALIGN_TOP_LEFT, 86, ROW_CLOCK_Y);

    /* 模式 + 参数 */
    s_lbl_mode   = ui_label_create(s_scr_main, &lv_font_montserrat_12,
                                   LV_ALIGN_TOP_MID, 0, ROW_MODE_Y);

    /* 闹钟倒计时 */
    s_img_bell  = ui_icon_create(s_scr_main, &s_icon_bell_dsc,
                                  LV_ALIGN_TOP_LEFT, 29, ROW_ALARM_Y);
    s_lbl_alarm  = ui_label_create(s_scr_main, &lv_font_montserrat_12,
                                   LV_ALIGN_TOP_LEFT, 43, ROW_ALARM_Y);

    lv_label_set_text(s_lbl_top,    "--\xC2\xB0" "C --%");
    lv_label_set_text(s_lbl_clock_hh, "00");
    lv_label_set_text(s_lbl_clock_mm, "00");
    lv_label_set_text(s_lbl_clock_ss, "00");
    lv_label_set_text(s_lbl_mode,   "");
    lv_label_set_text(s_lbl_alarm,  "");
}

/* ====== 闹钟设置界面创建 ====== */
static void ui_create_alarm(void)
{
    s_scr_alarm = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_alarm, lv_color_black(), 0);

    /* 标题 */
    ui_label_create(s_scr_alarm, &lv_font_montserrat_12,
                    LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_t *title = lv_obj_get_child(s_scr_alarm, 0);
    lv_label_set_text(title, "SET ALARM");

    /* HH:MM:SS 三段 (font 24) */
    const lv_coord_t y_time = 20;
    s_lbl_alarm_hh = ui_label_create(s_scr_alarm, &lv_font_montserrat_24,
                                     LV_ALIGN_TOP_LEFT, 14, y_time);
    ui_label_create(s_scr_alarm, &lv_font_montserrat_24,
                    LV_ALIGN_TOP_LEFT, 42, y_time);
    lv_label_set_text(lv_obj_get_child(s_scr_alarm, 2), ":");
    s_lbl_alarm_mm = ui_label_create(s_scr_alarm, &lv_font_montserrat_24,
                                     LV_ALIGN_TOP_LEFT, 50, y_time);
    ui_label_create(s_scr_alarm, &lv_font_montserrat_24,
                    LV_ALIGN_TOP_LEFT, 78, y_time);
    lv_label_set_text(lv_obj_get_child(s_scr_alarm, 4), ":");
    s_lbl_alarm_ss = ui_label_create(s_scr_alarm, &lv_font_montserrat_24,
                                     LV_ALIGN_TOP_LEFT, 86, y_time);

    lv_label_set_text(s_lbl_alarm_hh, "00");
    lv_label_set_text(s_lbl_alarm_mm, "00");
    lv_label_set_text(s_lbl_alarm_ss, "00");

    /* 按键提示 */
    s_lbl_alarm_hint = ui_label_create(s_scr_alarm, &lv_font_montserrat_12,
                                       LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_label_set_text(s_lbl_alarm_hint, "M:+  A:Next");
}

/* ====== 主界面刷新 ====== */
static void ui_refresh_main(void)
{
    char buf[32];
    time_t now;
    struct tm ti;

    time(&now);
    localtime_r(&now, &ti);

    /* 顶部: 温度 (°C) + 湿度 */
    snprintf(buf, sizeof(buf), "%02d-%02d %02d\xC2\xB0" "C %02d%%", ti.tm_mon + 1, ti.tm_mday, g_temp, g_humi);
    lv_label_set_text(s_lbl_top, buf);

    /* 状态图标: WiFi / BLE / 闹钟计时 */
    {
        EventBits_t bits = xEventGroupGetBits(g_system_events);
        lv_obj_add_flag(s_img_wifi, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_img_ble, LV_OBJ_FLAG_HIDDEN);
        if (bits & EVT_WIFI_CONNECTED) {
            lv_obj_remove_flag(s_img_wifi, LV_OBJ_FLAG_HIDDEN);
        }
        if (bits & EVT_BLE_CONNECTED) {
            lv_obj_remove_flag(s_img_ble, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* 大字时钟 HH:MM:SS (固定位置) */
    snprintf(buf, sizeof(buf), "%02d", ti.tm_hour);
    lv_label_set_text(s_lbl_clock_hh, buf);
    snprintf(buf, sizeof(buf), "%02d", ti.tm_min);
    lv_label_set_text(s_lbl_clock_mm, buf);
    snprintf(buf, sizeof(buf), "%02d", ti.tm_sec);
    lv_label_set_text(s_lbl_clock_ss, buf);

    /* 模式 + 参数 */
    {
        const char *mn = (g_mode < MODE_NAME_COUNT) ? s_mode_names[g_mode] : "?";
        switch (g_mode) {
        case LAMP_MODE_NORMAL:
        case LAMP_MODE_COLD:
        case LAMP_MODE_WARM:
            snprintf(buf, sizeof(buf), "%s  Lv%d", mn, g_light_level);
            break;
        case LAMP_MODE_COLOR:
            snprintf(buf, sizeof(buf), "%s  C%02d", mn, g_color_index);
            break;
        case LAMP_MODE_NIGHT:
            snprintf(buf, sizeof(buf), "%s", mn);
            break;
        case LAMP_MODE_STUDY:
            snprintf(buf, sizeof(buf), "%s  %02u:%02u", mn,
                     (unsigned)(g_study_time / 60U),
                     (unsigned)(g_study_time % 60U));
            break;
        case LAMP_MODE_AUTO:
            snprintf(buf, sizeof(buf), "%s  C%d", mn, g_color_index);
            break;
        default:
            snprintf(buf, sizeof(buf), "%s", mn);
            break;
        }
        lv_label_set_text(s_lbl_mode, buf);
    }

    /* 闹钟倒计时 */
    if (g_flag_count != 0U) {
        uint32_t rest = alarm_get_remaining();
        snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                 (unsigned)(rest / 3600U),
                 (unsigned)((rest % 3600U) / 60U),
                 (unsigned)(rest % 60U));
    } else {
        snprintf(buf, sizeof(buf), "--:--:--");
    }
    lv_label_set_text(s_lbl_alarm, buf);
}

/* ====== 闹钟设置界面刷新 (编辑字段闪烁) ====== */
static void ui_refresh_alarm(void)
{
    char buf[4];

    snprintf(buf, sizeof(buf), "%02d", g_alarm_hour);
    lv_label_set_text(s_lbl_alarm_hh, buf);
    snprintf(buf, sizeof(buf), "%02d", g_alarm_min);
    lv_label_set_text(s_lbl_alarm_mm, buf);
    snprintf(buf, sizeof(buf), "%02d", g_alarm_sec);
    lv_label_set_text(s_lbl_alarm_ss, buf);

    /* 先全部显示, 再隐藏当前编辑字段实现闪烁 */
    lv_obj_remove_flag(s_lbl_alarm_hh, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_lbl_alarm_mm, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_lbl_alarm_ss, LV_OBJ_FLAG_HIDDEN);
    if (s_blink == 1U) {
        switch (g_alarm_state) {
        case ALARM_SET_HOUR: lv_obj_add_flag(s_lbl_alarm_hh, LV_OBJ_FLAG_HIDDEN); break;
        case ALARM_SET_MIN:  lv_obj_add_flag(s_lbl_alarm_mm, LV_OBJ_FLAG_HIDDEN); break;
        case ALARM_SET_SEC:  lv_obj_add_flag(s_lbl_alarm_ss, LV_OBJ_FLAG_HIDDEN); break;
        default: break;
        }
    }

    lv_label_set_text(s_lbl_alarm_hint,
                      (g_alarm_state == ALARM_SET_CONFIRM) ? "M:Quit A:OK"
                                                           : "M:+  A:Next");
}

/* ====== 周期刷新 (LVGL 任务上下文, 250ms) ====== */
static void ui_update_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (g_alarm_state != ALARM_SET_IDLE) {
        if (lv_screen_active() != s_scr_alarm) {
            lv_screen_load(s_scr_alarm);
        }
        ui_refresh_alarm();
    } else {
        if (lv_screen_active() != s_scr_main) {
            lv_screen_load(s_scr_main);
        }
        ui_refresh_main();
    }
    s_blink ^= 1U;
}

/* ====== 公共 API ====== */

void display_init(void)
{
    /* 1. I2C 总线 (与 1.0.1 相同的引脚/速率) */
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .i2c_port = I2C_MASTER_PORT,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus));

    /* 2. SSD1306 Panel IO (I2C, 控制字节 1, 地址 0x3C) */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = SSD1306_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
        .control_phase_bytes = 1,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_bit_offset = 6,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &io_cfg, &io_handle));

    /* 3. SSD1306 面板 (128x64) */
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_ssd1306_config_t ssd1306_cfg = {
        .height = LCD_V_RES,
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,
        .vendor_config = &ssd1306_cfg,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true)); /* black bg, white text */
    s_panel = panel;

    ESP_LOGI(TAG, "SSD1306 128x64 via esp_lcd: I2C0 SDA=GPIO%u SCL=GPIO%u",
             I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);

    /* 4. LVGL + esp_lvgl_port */
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel,
        .buffer_size = LCD_H_RES * LCD_V_RES,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = true,
        .color_format = LV_COLOR_FORMAT_I1,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .sw_rotate = false,
        },
    };
    s_disp = lvgl_port_add_disp(&disp_cfg);
    if (s_disp == NULL) {
        ESP_LOGE(TAG, "Failed to add LVGL display");
        return;
    }

    /* 5. 创建 UI (LVGL API 非线程安全, 需持锁) */
    if (lvgl_port_lock(0)) {
        lv_disp_set_rotation(s_disp, LV_DISPLAY_ROTATION_0);
        ui_create_main();
        ui_create_alarm();
        lvgl_port_unlock();
    }

    ESP_LOGI(TAG, "LVGL 9 UI created (128x64, I1 full buffer)");
}

void oled_task_init(void)
{
    /* 时区设为北京时间 (UTC+8), 必须在 localtime_r 调用之前设定 */
    setenv("TZ", "CST-8", 1);
    tzset();

    display_init();

    /* LVGL 自己的任务会调用 lv_timer_handler, 周期刷新回调注册在这里 */
    lv_timer_create(ui_update_timer_cb, 250, NULL);

    ESP_LOGI(TAG, "OLED/LVGL refresh timer started (250ms)");
}

void display_update(const oled_data_t *data)
{
    /* LVGL 版本由定时器直接从全局状态刷新, 保留该接口以兼容调用方 */
    (void)data;
}

void display_suspend(void)
{
    if (s_panel != NULL) {
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, false));
        ESP_LOGI(TAG, "OLED display off (suspend)");
    }
}

void display_resume(void)
{
    if (s_panel != NULL) {
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
        ESP_LOGI(TAG, "OLED display on (resume)");
    }
}
