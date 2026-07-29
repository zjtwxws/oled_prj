/**
 * @file    led_mgr.h
 * @brief   LED 状态管理器
 */

#ifndef __LED_MGR_H
#define __LED_MGR_H

#include <stdint.h>

typedef enum {
    LED_STATE_OFF = 0,
    LED_STATE_ON  = 1,
    LED_STATE_BLINK = 2
} led_state_t;

#define LED_BLINK_PERIOD_MS  500

void led_mgr_init(void);                 /* GPIO 初始化 */
void led_mgr_set_state(led_state_t s);   /* 切换状态 */
led_state_t led_mgr_get_state(void);
void led_mgr_tick(uint32_t elapsed_ms);  /* 定时 tick(用于闪烁) */

#endif
