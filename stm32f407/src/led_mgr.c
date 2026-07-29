/**
 * @file    led_mgr.c
 * @brief   LED 管理器实现
 *
 * 依赖: STM32 HAL GPIO
 * 用户需在 CubeMX 中配置 LED 引脚为 GPIO_Output,
 * 并在 led_mgr_init() 下方的 HAL_GPIO_WritePin 中添加实际端口和引脚。
 */

#include "led_mgr.h"
#include "stm32f4xx_hal.h"

/* ---- 用户配置: LED 端口 & 引脚 ---- */
#define LED_PORT    GPIOB
#define LED_PIN     GPIO_PIN_0  /* TODO: 根据实际板卡修改 */

static led_state_t led_state = LED_STATE_OFF;
static uint32_t blink_timer = 0;
static uint8_t  blink_on = 0;

void led_mgr_init(void)
{
    /* GPIO 初始化在 CubeMX 生成的 MX_GPIO_Init() 中完成 */
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
}

void led_mgr_set_state(led_state_t s)
{
    led_state = s;
    blink_timer = 0;
    blink_on = 0;

    switch (s) {
    case LED_STATE_OFF:
        HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
        break;
    case LED_STATE_ON:
        HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
        break;
    case LED_STATE_BLINK:
        blink_on = 1;
        HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
        break;
    }
}

led_state_t led_mgr_get_state(void)
{
    return led_state;
}

void led_mgr_tick(uint32_t elapsed_ms)
{
    if (led_state != LED_STATE_BLINK) return;

    blink_timer += elapsed_ms;
    if (blink_timer >= LED_BLINK_PERIOD_MS) {
        blink_timer = 0;
        blink_on = !blink_on;
        HAL_GPIO_WritePin(LED_PORT, LED_PIN, blink_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}
