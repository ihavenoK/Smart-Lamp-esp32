#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

/**
 * @brief WiFi 凭证 + OTA 固件升级配置
 *
 * 通过 idf.py menuconfig -> SmartLamp Configuration
 * 设置 SSID/密码/OTA URL。sdkconfig 已在 .gitignore, 不会泄露。
 */

#define DEFAULT_WIFI_SSID     CONFIG_SMARTLAMP_WIFI_SSID
#define DEFAULT_WIFI_PASS     CONFIG_SMARTLAMP_WIFI_PASSWORD

/* OTA 固件下载地址 — menuconfig 中配置 */
#define OTA_FIRMWARE_URL      CONFIG_SMARTLAMP_OTA_FIRMWARE_URL
#define OTA_VERSION_URL       CONFIG_SMARTLAMP_OTA_VERSION_URL

#endif /* WIFI_CONFIG_H */
