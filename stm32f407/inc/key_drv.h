/**
 * @file    key_drv.h
 * @brief   按键驱动 (消抖 + 长按检测)
 *
 * 参照厂家示例接线 (HelTec OLED+按键模组):
 *   KEY1: PA1, KEY2: PA2, KEY3: PA3, KEY4: PA4 (全部上拉输入, 按下=低电平)
 *
 * 接口: STM32 HAL GPIO, 在 CubeMX 中配置引脚为 GPIO_Input 上拉模式。
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

/*
 * 端口配置宏 — 使用前需确保 CubeMX 已生成对应的 GPIO 初始化代码。
 * 若没有 4 键, 可将 KEY_PIN3/KEY_PIN4 定义为 0 并修改 KEY_COUNT。
 */
#define KEY_PORT1   GPIOA
#define KEY_PIN1    GPIO_PIN_1
#define KEY_PORT2   GPIOA
#define KEY_PIN2    GPIO_PIN_2
/* KEY3 / KEY4 可选, 按需启用在 key_drv.c 中 */
#define KEY_PORT3   GPIOA
#define KEY_PIN3    GPIO_PIN_3
#define KEY_PORT4   GPIOA
#define KEY_PIN4    GPIO_PIN_4

#define KEY_COUNT   2  /* 启用 KEY1~KEY2 (可改为 4 启用全部) */

void key_drv_init(void);
int key_drv_scan(key_info_t *info);
void key_drv_exti_callback(uint8_t key_id);

#endif
