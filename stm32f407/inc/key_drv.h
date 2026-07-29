/**
 * @file    key_drv.h
 * @brief   按键驱动 (消抖 + 长按检测)
 */

#ifndef __KEY_DRV_H
#define __KEY_DRV_H

#include <stdint.h>

#define KEY_DEBOUNCE_MS    20
#define KEY_LONG_PRESS_MS  2000

typedef enum {
    KEY_EVENT_NONE        = 0,
    KEY_EVENT_SHORT_PRESS = 1,
    KEY_EVENT_LONG_PRESS  = 2,
    KEY_EVENT_RELEASE     = 3
} key_event_t;

typedef struct {
    uint8_t key_id;
    key_event_t event;
} key_info_t;

/**
 * @brief  按键初始化 (GPIO 配置在 CubeMX 中完成)
 */
void key_drv_init(void);

/**
 * @brief  按键扫描 tick (每 10~20ms 调用一次)
 * @return 有事件时返回 1, key_info 被填充; 否则 0
 */
int key_drv_scan(key_info_t *info);

/**
 * @brief  外部中断回调 (按需使用)
 */
void key_drv_exti_callback(uint8_t key_id);

#endif
