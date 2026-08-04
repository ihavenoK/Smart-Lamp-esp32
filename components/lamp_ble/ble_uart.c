/**
 * @file ble_uart.c
 * @brief BLE UART 透传 (Nordic NUS) — 严格对齐 ESP-IDF 官方 ble_uart_nimble.c
 *
 * 基于 ESP-IDF 6.0.2 ble_uart_service 官方例程重写。
 * 关键: GATT 表在运行时通过 build_gatt_table() 赋值, 不用 static const。
 */

#include "ble_uart.h"
#include "main.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>

/* NimBLE (官方例程头文件集合) */
#include "nimble/ble.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_uart";

extern void ble_store_config_init(void);

/* ====== 设备名 ====== */
#define DEVICE_NAME "SmartLamp"

/* ====== NUS UUID (同官方例程 little-endian 字节序) ====== */
#define NUS_SVC_BYTES  0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, \
                       0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e
#define NUS_RX_BYTES   0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, \
                       0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e
#define NUS_TX_BYTES   0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, \
                       0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e

static const ble_uuid128_t s_svc_uuid    = BLE_UUID128_INIT(NUS_SVC_BYTES);
static const ble_uuid128_t s_chr_rx_uuid = BLE_UUID128_INIT(NUS_RX_BYTES);
static const ble_uuid128_t s_chr_tx_uuid = BLE_UUID128_INIT(NUS_TX_BYTES);

/* ====== BLE 状态 ====== */
#define DEV_NAME_MAX 32
static char     s_dev_name[DEV_NAME_MAX];
static uint16_t s_tx_val_handle;
static uint16_t s_rx_val_handle;
static volatile uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint8_t  s_own_addr_type;

/* ====== GATT 表: 运行时赋值 (不是 static const) ====== */
static struct ble_gatt_chr_def s_chr_defs[3];
static struct ble_gatt_svc_def s_svc_defs[2];

static int start_advertising(void);
static int gap_event(struct ble_gap_event *event, void *arg);

/* ====== RX 写回调: 手机 → 设备 ====== */
static int chr_access(uint16_t conn_handle, uint16_t attr_handle,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t total = OS_MBUF_PKTLEN(ctxt->om);
    if (total < 3) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    /* 支持两种帧格式:
     *   [mode][light][color]            → 旧协议 3 字节, cmd_type=SET
     *   [cmd_type][mode][light][color]  → 新协议 4 字节 (支持闹钟等命令)
     */
    uint8_t buf[4] = {0};
    uint16_t copied = 0;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, total, &copied);
    if (rc != 0 || copied < 3) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    lamp_cmd_t cmd = {
        .source = CMD_SRC_BLE,
    };

    if (copied >= 4) {
        /* 新协议: [cmd_type][mode][light][color] */
        cmd.cmd_type = buf[0];
        cmd.mode     = buf[1];
        cmd.light    = buf[2];
        cmd.color    = buf[3];
        ESP_LOGI(TAG, "RX cmd: type=%u mode=%u light=%u color=%u",
                 cmd.cmd_type, cmd.mode, cmd.light, cmd.color);
    } else {
        /* 旧协议: [mode][light][color] */
        cmd.cmd_type = CMD_TYPE_SET;
        cmd.mode     = buf[0];
        cmd.light    = buf[1];
        cmd.color    = buf[2];
        ESP_LOGI(TAG, "RX cmd (legacy): mode=%u light=%u color=%u",
                 cmd.mode, cmd.light, cmd.color);
    }

    xQueueSend(g_cmd_queue, &cmd, 0);
    return 0;
}

/* ====== GATT 表运行时构建 ====== */
static void build_gatt_table(void)
{
    s_chr_defs[0] = (struct ble_gatt_chr_def){
        .uuid       = &s_chr_rx_uuid.u,
        .access_cb  = chr_access,
        .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .val_handle = &s_rx_val_handle,
    };
    s_chr_defs[1] = (struct ble_gatt_chr_def){
        .uuid       = &s_chr_tx_uuid.u,
        .access_cb  = chr_access,
        .flags      = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_tx_val_handle,
    };
    s_chr_defs[2] = (struct ble_gatt_chr_def){0};

    s_svc_defs[0] = (struct ble_gatt_svc_def){
        .type            = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid            = &s_svc_uuid.u,
        .characteristics = s_chr_defs,
    };
    s_svc_defs[1] = (struct ble_gatt_svc_def){0};
}

/* ====== TX: 设备 → 手机 (Notify) ====== */
static void ble_send_state(const ble_state_t *data)
{
    uint16_t conn = s_conn_handle;
    if (conn == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, sizeof(ble_state_t));
    if (om == NULL) {
        return;
    }
    int rc = ble_gatts_notify_custom(conn, s_tx_val_handle, om);
    if (rc != 0) {
        ESP_LOGD(TAG, "notify failed: %d", rc);
    }
}

/* ====== 广播 ====== */
static int start_advertising(void)
{
    size_t name_len = strlen(s_dev_name);

    struct ble_hs_adv_fields adv = {
        .flags                 = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .name                  = name_len > 0 ? (uint8_t *)s_dev_name : NULL,
        .name_len              = name_len,
        .name_is_complete      = name_len > 0 ? 1 : 0,
    };
    int rc = ble_gap_adv_set_fields(&adv);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields rc=%d", rc);
        return rc;
    }

    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_start rc=%d", rc);
        return rc;
    }
    ESP_LOGI(TAG, "advertising as '%s'", s_dev_name);
    return 0;
}

/* ====== GAP 事件 (直接传入 ble_gap_adv_start, 不用 listener) ====== */
static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            xEventGroupSetBits(g_system_events, EVT_BLE_CONNECTED);
            ESP_LOGI(TAG, "connected, handle=%u", s_conn_handle);
        } else {
            ESP_LOGW(TAG, "connect failed, status=%d", event->connect.status);
            start_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected, reason=%d", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        xEventGroupClearBits(g_system_events, EVT_BLE_CONNECTED);
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe attr=%u notify=%d",
                 event->subscribe.attr_handle, event->subscribe.cur_notify);
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated: %u", event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

/* ====== NimBLE Host 回调 ====== */
static void on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE reset, reason=%d", reason);
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "infer addr type rc=%d", rc);
        return;
    }

    start_advertising();
}

static void nimble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ====== NimBLE 初始化 (app_main 线程, 同步完成) ====== */
void ble_uart_init(void)
{
    int rc;

    /* 1. nimble_port_init (官方: 先 init, 再设 ble_hs_cfg) */
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", err);
        return;
    }

    /* 2. ble_hs_cfg */
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb  = on_sync;

    /* 3. SM 配置 (对齐官方, 不加密模式也必须显式) */
    ble_hs_cfg.sm_io_cap  = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_sc      = 0;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm    = 0;

    /* 4. 内置服务 */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    /* 5. 设备名 */
    strncpy(s_dev_name, DEVICE_NAME, sizeof(s_dev_name) - 1);
    s_dev_name[sizeof(s_dev_name) - 1] = '\0';

    /* 6. 运行时构建 GATT 表 */
    build_gatt_table();

    /* 7. GATT 注册 */
    rc = ble_gatts_count_cfg(s_svc_defs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return;
    }
    rc = ble_gatts_add_svcs(s_svc_defs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "GATT services registered OK");

    /* 8. NVS bond store */
    ble_store_config_init();
}

/* ====== BLE 任务 (仅启动 Host + 主循环) ====== */
static void ble_uart_task(void *arg)
{
    ESP_LOGI(TAG, "BLE host task starting...");

    /* 启动 NimBLE Host (on_sync → start_advertising) */
    nimble_port_freertos_init(nimble_host_task);

    /* 主循环: 定时上报 */
    ble_state_t state;
    while (1) {
        if (xQueueReceive(g_ble_upload_queue, &state, pdMS_TO_TICKS(500)) == pdPASS) {
            ble_send_state(&state);
        }
    }
}

void ble_uart_task_init(void)
{
    xTaskCreate(ble_uart_task, "ble_uart", 4096, NULL, 8, NULL);
}
