#ifndef LAMP_WIFI_H
#define LAMP_WIFI_H

/**
 * @file wifi.h
 * @brief WiFi STA 管理 + SNTP 北京时间同步 + OTA 固件升级
 *
 * 从原 cloud.c 拆出，巴法云已移除。
 * WiFi 用途:
 *   - SNTP 获取北京时间 (OLED 时钟)
 *   - OTA 上电自动检查并下载新固件
 */

/**
 * @brief 启动 WiFi 连接 + SNTP 时间同步任务 (优先级 10)
 *
 * 流程:
 *   1. 初始化 TCP/IP 栈 + WiFi STA 模式
 *   2. 连接 AP (SSID/密码从 menuconfig 读取)
 *   3. 获取 IP 后通过 SNTP 同步北京时间
 *   4. 同步成功置 EVT_TIME_SYNCED 事件位
 *   5. 执行 OTA 固件升级检查
 *
 * 任务在完成 OTA 检查后自删除 (如 OTA 成功则 esp_restart)。
 */
void wifi_task_init(void);

/**
 * @brief OTA 固件升级检查
 *
 * 由 wifi_task 在 SNTP 同步后调用。
 * 流程:
 *   1. HTTP GET version.txt → 对比本地版本号 CONFIG_APP_PROJECT_VER
 *   2. 版本不同 → HTTP GET smartlamp.bin → esp_https_ota() → esp_restart()
 *   3. 版本相同/网络不通 → 跳过, 继续正常启动
 */
void ota_check(void);

#endif /* LAMP_WIFI_H */
