#ifndef KEY_H
#define KEY_H

#include <stdint.h>

/**
 * @brief 按键组件 — FreeRTOS 定时器驱动的非阻塞按键
 *
 * GPIO18 = MODE键 (模式切换), GPIO19 = ADJUST键 (亮度/颜色调节)
 * 50ms 定时器自动扫描, 按键按下 → lamp_cmd_t → g_cmd_queue
 */

/* 按键编号 */
#define KEY_MODE    1U
#define KEY_ADJUST  2U

/* 按键状态机 */
typedef enum {
    KEY_STATE_IDLE      = 0,
    KEY_STATE_DEBOUNCE  = 1,
    KEY_STATE_PRESSED   = 2,
    KEY_STATE_HOLD      = 3,
    KEY_STATE_RELEASE   = 4
} key_state_t;

/**
 * @brief 初始化按键 + 启动 50ms 扫描定时器
 */
void key_init(void);

#endif /* KEY_H */
