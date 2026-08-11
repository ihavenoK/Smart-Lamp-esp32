#ifndef LAMP_PM_H
#define LAMP_PM_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/**
 * @brief 低功耗管理组件 — Light Sleep 睡眠/唤醒 + 时钟备份
 *
 * 唤醒源:
 *   - EXT1: GPIO32(MODE键) / GPIO14(ADJUST键), ANY_HIGH (按下=高, 按键反接 3.3V)
 *   - Timer: 30 分钟自动唤醒兜底 (防死睡)
 *
 * 注意: ESP32 的 EXT1 仅支持 ANY_HIGH/ALL_LOW, 且唤醒引脚必须是 RTC GPIO
 *       (GPIO0-15/32-39)。按键原 GPIO18/19 无法唤醒, 已改接 32/14。
 *       GPIO13 已分配给 LD2402 雷达 RX, MODE 键避开该引脚。
 *
 * 守卫条件 (lamp_core 判定, 任一为真则不睡):
 *   - BLE 已连接
 *   - 闹钟/学习计时运行中
 *   - 雷达检测到人
 */

/**
 * @brief 初始化睡眠配置 (EXT1 唤醒源 + 兜底定时器)
 * 在 app_main 末尾调用一次
 */
void lamp_pm_init(void);

/**
 * @brief 进入 Light Sleep (阻塞直到被唤醒)
 *
 * 流程: 灭灯 → 关 OLED → 存时间 → esp_light_sleep_start()
 *       → 恢复时间 → 开 OLED
 */
void lamp_pm_enter_sleep(void);

/**
 * @brief 保存当前 Unix 时间到 RTC_SLOW_MEM
 * Light Sleep 下系统时间由 RTC 硬件自动保持, 此备份为
 * 未来 Deep Sleep 扩展预留 + 系统时间异常时的兜底
 */
void lamp_pm_save_time(void);

/**
 * @brief 从 RTC_SLOW_MEM 恢复时间
 * @return 恢复后的 Unix 时间戳 (秒)
 */
time_t lamp_pm_restore_time(void);

#endif /* LAMP_PM_H */
