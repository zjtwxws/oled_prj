/**
 * @file    key_drv.c
 * @brief   按键驱动实现 (轮询扫描 + 消抖 + 长按)
 */

#include "key_drv.h"
#include "stm32f4xx_hal.h"
#include <string.h>

/* ---- 用户配置: 按键端口 & 引脚 (KEY1, KEY2) ---- */
#define KEY_COUNT           2
#define DEBOUNCE_SAMPLES    3   /* 需要连续 3 次相同读数才算稳定 (3×20ms=60ms) */

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint8_t       last_raw;       /* 上一次读取的原始电平 */
    uint8_t       stable_state;   /* 消抖后的稳定状态 (0=按下, 1=释放) */
    uint8_t       debounce_cnt;   /* 连续相同读数的计数 (用于消抖) */
    uint32_t      press_timer;    /* 持续按下计时 (用于长按检测) */
    uint8_t       event_pending;
    key_event_t   pending_event;
    uint8_t       long_press_fired; /* 长按是否已触发过 (防止重复触发) */
} key_dev_t;

static key_dev_t keys[KEY_COUNT];

void key_drv_init(void)
{
    memset(keys, 0, sizeof(keys));

    /* KEY1: PA0 (示例, 根据实际修改) */
    keys[0].port = GPIOA;
    keys[0].pin  = GPIO_PIN_0;

    /* KEY2: PE4 (示例) */
    keys[1].port = GPIOE;
    keys[1].pin  = GPIO_PIN_4;
}

int key_drv_scan(key_info_t *info)
{
    /* 检测待处理事件 */
    for (int i = 0; i < KEY_COUNT; i++) {
        if (keys[i].event_pending) {
            info->key_id = i + 1;
            info->event  = keys[i].pending_event;
            keys[i].event_pending = 0;
            return 1;
        }
    }

    /* 轮询扫描 */
    for (int i = 0; i < KEY_COUNT; i++) {
        uint8_t raw = (HAL_GPIO_ReadPin(keys[i].port, keys[i].pin) == GPIO_PIN_RESET) ? 0 : 1;
        /* 上拉输入: 按下=0, 释放=1 */

        if (raw == keys[i].last_raw) {
            /* 电平不变, 累加连续计数 */
            if (keys[i].debounce_cnt < DEBOUNCE_SAMPLES) {
                keys[i].debounce_cnt++;
            }

            /* 消抖完成: 连续 N 次相同读数, 确认状态变化 */
            if (keys[i].debounce_cnt == DEBOUNCE_SAMPLES &&
                raw != keys[i].stable_state) {
                keys[i].stable_state = raw;

                if (raw == 0) {
                    /* 确认按下 */
                    keys[i].press_timer = 0;
                    keys[i].long_press_fired = 0;
                } else {
                    /* 确认释放 — 仅短按触发, 长按由按下计时段触发 */
                    if (!keys[i].long_press_fired) {
                        keys[i].pending_event = KEY_EVENT_SHORT_PRESS;
                        keys[i].event_pending = 1;
                    }
                    keys[i].press_timer = 0;
                }
            }
        } else {
            /* 电平变化, 重置消抖计数 */
            keys[i].last_raw = raw;
            keys[i].debounce_cnt = 0;
        }

        /* 按下中, 累加按下计时 */
        if (keys[i].stable_state == 0) {
            keys[i].press_timer += KEY_DEBOUNCE_MS;
            /* 长按检测: 到达阈值且尚未触发过 */
            if (!keys[i].long_press_fired &&
                keys[i].press_timer >= KEY_LONG_PRESS_MS) {
                keys[i].long_press_fired = 1;
                keys[i].pending_event = KEY_EVENT_LONG_PRESS;
                keys[i].event_pending = 1;
            }
        }
    }
    return 0;
}

void key_drv_exti_callback(uint8_t key_id)
{
    if (key_id < 1 || key_id > KEY_COUNT) return;
    /* 外部中断模式下可在此处设置标志 */
}
