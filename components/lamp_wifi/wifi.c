/**
 * @file wifi.c
 * @brief WiFi STA 管理 + SNTP 北京时间同步 + OTA 固件升级
 *
 * 从原 cloud.c 拆出，巴法云已移除。
 * WiFi 用途:
 *   - SNTP 获取北京时间 (OLED 时钟)
 *   - OTA 上电自动检查并下载新固件
 *
 * OTA 流程 (在 SNTP 同步后执行):
 *   1. HTTP GET version.txt → strcmp 对比本地 CONFIG_APP_PROJECT_VER
 *   2. 版本不同 → HTTP GET smartlamp.bin → esp_https_ota()
 *   3. OTA 成功 → esp_restart() (bootloader 会从新分区启动)
 *   4. 版本相同/网络不通/OTA 失败 → 跳过, 继续正常启动
 *
 * 使用 esp_https_ota() 简化 API:
 *   无需手动 esp_ota_begin/write/end/set_boot_partition,
 *   一个函数完成下载+写入+分区标记, 比 native API 省 80% 代码。
 */

#include "wifi.h"
#include "wifi_config.h"
#include "main.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_netif_sntp.h"     /* v6.0: 替代 esp_sntp.h */
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>
#include <time.h>

static const char *TAG = "wifi";

/* ====== WiFi 事件回调 ====== */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(g_system_events, EVT_WIFI_CONNECTED);
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(g_system_events, EVT_WIFI_CONNECTED);
        ESP_LOGI(TAG, "WiFi connected, got IP");
    }
}

/* ====== OTA 版本检查 ====== */

/**
 * @brief 从 HTTP 服务器读取版本字符串
 *
 * 发送 GET 请求到 OTA_VERSION_URL, 读取响应体中的版本号。
 * 服务器返回的 version.txt 应仅包含版本字符串 (如 "1.0.1"),
 * 末尾可有换行符 (会被截断)。
 *
 * @param buf     [out] 缓冲区, 存放版本号
 * @param buf_len 缓冲区大小 (建议 >= 16)
 * @return 0 成功, -1 失败
 */
static int ota_fetch_version(char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0U) return -1;

    esp_http_client_config_t cfg = {
        .url        = OTA_VERSION_URL,
        .method     = HTTP_METHOD_GET,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "OTA: Failed to init HTTP client for version check");
        return -1;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OTA: Cannot reach version URL: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return -1;
    }

    int status = esp_http_client_fetch_headers(client);
    if (status < 0) {
        ESP_LOGW(TAG, "OTA: Version fetch headers failed");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return -1;
    }

    int read_len = esp_http_client_read(client, buf, (int)(buf_len - 1U));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (read_len <= 0) {
        ESP_LOGW(TAG, "OTA: Version file empty or unreadable");
        return -1;
    }

    buf[read_len] = '\0';

    /* 去掉末尾换行符 (\n / \r\n) */
    int end = read_len - 1;
    while (end >= 0 && (buf[end] == '\n' || buf[end] == '\r')) {
        buf[end] = '\0';
        end--;
    }

    ESP_LOGI(TAG, "OTA: Server version = \"%s\", local = \"" CONFIG_APP_PROJECT_VER "\"",
             buf);
    return 0;
}

/**
 * @brief 从 HTTP 服务器下载固件并执行 OTA 升级
 *
 * 使用 esp_https_ota() 简化 API:
 *   1. 配置 HTTP URL (指向 .bin 文件)
 *   2. 调用 esp_https_ota() 完成下载、校验、写入分区、标记启动分区
 *   3. 成功 → esp_restart()
 *
 * 升级过程自动选择空闲 OTA 分区 (ota_0 或 ota_1),
 * 不覆盖当前运行的 factory 分区。
 */
static void ota_do_upgrade(void)
{
    ESP_LOGI(TAG, "OTA: Downloading firmware from %s", OTA_FIRMWARE_URL);

    esp_http_client_config_t http_cfg = {
        .url        = OTA_FIRMWARE_URL,
        .method     = HTTP_METHOD_GET,
        .timeout_ms = 30000,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t ret = esp_https_ota(&ota_cfg);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA: Upgrade successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000));  /* 让日志刷出 */
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA: Upgrade failed, error: %s (0x%x)",
                 esp_err_to_name(ret), ret);
        ESP_LOGW(TAG, "OTA: Will continue with current firmware");
    }
}

/**
 * @brief OTA 固件升级检查入口
 *
 * 调用时机: wifi_task 中 SNTP 时间同步完成后
 *
 * 策略:
 *   如果配置了版本检查 URL:
 *     1. GET 版本号
 *     2. 与本地 CONFIG_APP_PROJECT_VER 对比
 *     3. 不同 → 下载升级
 *     4. 相同 → 跳过
 *
 *   如果没配置版本 URL (OTA_VERSION_URL 为空):
 *     跳过 OTA (安全默认——开发阶段不配 URL 就不会自动刷)
 */
void ota_check(void)
{
#if defined(CONFIG_SMARTLAMP_OTA_ENABLE) && CONFIG_SMARTLAMP_OTA_ENABLE

    /* 如果没有配置固件 URL, 跳过 OTA */
    const char *fw_url = OTA_FIRMWARE_URL;
    if (fw_url[0] == '\0') {
        ESP_LOGI(TAG, "OTA: Firmware URL not configured, skipping");
        return;
    }

    /* 版本检查: 仅当配置了 version URL 才做对比,
     * 否则直接下载 (适用于没有版本服务器的简易部署) */
    const char *ver_url = OTA_VERSION_URL;
    if (ver_url[0] != '\0') {
        char server_ver[32];
        if (ota_fetch_version(server_ver, sizeof(server_ver)) == 0) {
            /* 对比版本号: 相同则跳过, 不同则升级 */
            if (strcmp(server_ver, CONFIG_APP_PROJECT_VER) == 0) {
                ESP_LOGI(TAG, "OTA: Firmware up to date (%s)", server_ver);
                return;
            }
        } else {
            /* 版本检查失败 (网络不通等), 跳过升级, 不影响正常启动 */
            ESP_LOGW(TAG, "OTA: Version check failed, skipping upgrade");
            return;
        }
    }

    ota_do_upgrade();

#else
    ESP_LOGI(TAG, "OTA: Disabled in menuconfig, skipping");
#endif
}

/* ====== WiFi 主任务 ====== */

static void wifi_task(void *arg)
{
    /* 初始化 TCP/IP + WiFi */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               &wifi_event_handler, NULL);

    /* 配置 WiFi */
    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid     = DEFAULT_WIFI_SSID,
            .password = DEFAULT_WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* 等待连接 */
    EventBits_t bits = xEventGroupWaitBits(
        g_system_events, EVT_WIFI_CONNECTED,
        pdFALSE, pdTRUE, pdMS_TO_TICKS(30000));

    if (bits & EVT_WIFI_CONNECTED) {
        /* SNTP 时间同步 (v6.0 新 API)
         * CONFIG_LWIP_SNTP_MAX_SERVERS 默认=1, 用单服务器版本 */
        esp_sntp_config_t sntp_cfg =
            ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
        esp_netif_sntp_init(&sntp_cfg);

        /* 等待时间同步 (每次最多等 2 秒, 重试 10 次) */
        for (int32_t retry = 0; retry < 10; retry++) {
            if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(2000)) == ESP_OK) {
                time_t now;
                struct tm timeinfo;
                time(&now);
                localtime_r(&now, &timeinfo);
                if (timeinfo.tm_year >= (2025 - 1900)) {
                    xEventGroupSetBits(g_system_events, EVT_TIME_SYNCED);
                    ESP_LOGI(TAG, "Time synced via SNTP");
                    break;
                }
            }
        }

        /* OTA 固件升级检查 — WiFi 连上 + 时间同步后才执行
         * 如果 OTA 成功 → esp_restart() 不会走到下面的 vTaskDelete
         * 如果 OTA 失败/跳过 → 继续正常启动流程 */
        ota_check();
    }

    vTaskDelete(NULL);
}

void wifi_task_init(void)
{
    xTaskCreate(wifi_task, "wifi", 4096, NULL, 10, NULL);
}
