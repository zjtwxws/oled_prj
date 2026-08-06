/**
 * @file    led_mgr.h
 * @brief   LED 状态管理器
 *
 * LED 端口/引脚通过 led_mgr.c 中的 LED_PORT/LED_PIN 宏配置。
 * 默认 PB0, 用户需根据实际 STM32F407 板卡修改。
 * CubeMX 中需将对应引脚配置为 GPIO_Output (推挽, 无上拉下拉)。
 */

#ifndef __LED_MGR_H
#define __LED_MGR_H

#include <stdint.h>

typedef enum {
    LED_STATE_OFF = 0,   /* LED 关闭 */
    LED_STATE_ON  = 1,    /* LED 常亮 */
    LED_STATE_BLINK = 2 /* LED 闪烁 (周期 = LED_BLINK_PERIOD_MS) */
} led_state_t;

#define LED_BLINK_PERIOD_MS  500  /* LED 闪烁周期 (ms)，亮灭各占一半 */

/** @brief 初始化 LED (GPIO 已在 CubeMX MX_GPIO_Init 中配置) */
void led_mgr_init(void);
/** @brief 设置 LED 状态 (OFF/ON/BLINK) */
void led_mgr_set_state(led_state_t s);
/** @brief 获取当前 LED 状态 */
led_state_t led_mgr_get_state(void);
/** @brief LED 闪烁 tick (需周期性调用，elapsed_ms 为距离上次调用的毫秒数) */
void led_mgr_tick(uint32_t elapsed_ms);

#endif
