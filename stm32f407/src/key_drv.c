/**
 * @file    key_drv.c
 * @brief   按键驱动实现 (轮询扫描 + 消抖 + 长按 + 长按重复)
 *
 * 长按计时基于 HAL_GetTick() 真实时间, 不受主循环负载影响。
 * GPIO 初始化由 CubeMX 生成的 MX_GPIO_Init() 完成。
 */

#include "key_drv.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include "debug_console.h"

#define DEBOUNCE_SAMPLES    3   /* 连续 3 次相同 → 消抖完成 (3×20ms=60ms) */

typedef struct {
    GPIO_TypeDef *port;     /* GPIO 端口基地址 */
    uint16_t      pin;          /* GPIO 引脚号 */
    uint8_t       last_raw;       /* 上一次读取的原始电平 (用于边沿检测) */
    uint8_t       stable_state;  /* 消抖后的稳定状态 (0=按下, 1=释放) */
    uint8_t       debounce_cnt;  /* 消抖计数器 (连续相同次数) */
    uint32_t      press_start_tick;  /* 按下时刻的 HAL_GetTick() 值，用于真实时间长按检测 */
    uint8_t       event_pending;  /* 有待处理事件标志 */
    key_event_t   pending_event;  /* 待处理的事件类型 */
    uint8_t       long_press_fired;  /* 长按已触发标志 (防止重复触发) */
    uint32_t      last_repeat_tick;   /* 上次长按重复触发时刻 */   /* 上次重复触发时刻 */
    uint8_t       release_pending;    /* 长按释放后待处理的 RELEASE 事件标志 */    /* 是否有释放事件待处理 */
} key_dev_t;

/* 按键设备数组: 索引 0~3 对应物理按键 KEY1~KEY4 */
static key_dev_t keys[KEY_COUNT];

/* 辅助: 初始化一个按键结构 */
static void key_init_entry(key_dev_t *k, GPIO_TypeDef *port, uint16_t pin)
{
    memset(k, 0, sizeof(*k));
    k->port = port;
    k->pin  = pin;
    k->stable_state = 1;  /* 上拉输入, 默认释放 */
}

void key_drv_init(void)
{
    memset(keys, 0, sizeof(keys));

    /*
     * 引脚映射: 与厂家模组接线一致 (PE1~PE4)。
     * 如只启用 KEY_COUNT=2, 只初始化前两个。
     */
#if KEY_COUNT >= 1
    key_init_entry(&keys[0], KEY_PORT1, KEY_PIN1);
#endif
#if KEY_COUNT >= 2
    key_init_entry(&keys[1], KEY_PORT2, KEY_PIN2);
#endif
#if KEY_COUNT >= 3
    key_init_entry(&keys[2], KEY_PORT3, KEY_PIN3);
#endif
#if KEY_COUNT >= 4
    key_init_entry(&keys[3], KEY_PORT4, KEY_PIN4);
#endif
}

int key_drv_scan(key_info_t *info)
{
    /* 待处理事件优先返回 */
    for (int i = 0; i < KEY_COUNT; i++) {
        if (keys[i].event_pending) {
            info->key_id = i + 1;
            info->event  = keys[i].pending_event;
            keys[i].event_pending = 0;
            return 1;
        }
    }

    uint32_t now = HAL_GetTick();

    for (int i = 0; i < KEY_COUNT; i++) {
        uint8_t raw = (HAL_GPIO_ReadPin(keys[i].port, keys[i].pin) == GPIO_PIN_RESET) ? 0 : 1;

        if (raw == keys[i].last_raw) {
            if (keys[i].debounce_cnt < DEBOUNCE_SAMPLES) {
                keys[i].debounce_cnt++;
            }
            if (keys[i].debounce_cnt == DEBOUNCE_SAMPLES &&
                raw != keys[i].stable_state) {
                keys[i].stable_state = raw;

                if (raw == 0) {
                    /* 确认按下: 记录起始时刻 */
                    DEBUG_PRINTF("[KEY%d] Press", i + 1);
                    keys[i].press_start_tick = now;
                    keys[i].long_press_fired = 0;
                    keys[i].last_repeat_tick = 0;
                    keys[i].release_pending = 0;
                } else {
                    /* 确认释放 */
                    if (keys[i].long_press_fired) {
                        /* 长按后释放: 产生 RELEASE 事件 */
                        DEBUG_PRINTF("[KEY%d] Release (after long press)", i + 1);
                        keys[i].release_pending = 1;
                        keys[i].pending_event = KEY_EVENT_RELEASE;
                        keys[i].event_pending = 1;
                        keys[i].long_press_fired = 0;
                    } else {
                        /* 短按: 产生 SHORT_PRESS 事件 */
                        DEBUG_PRINTF("[KEY%d] Short Press", i + 1);
                        keys[i].pending_event = KEY_EVENT_SHORT_PRESS;
                        keys[i].event_pending = 1;
                    }
                }
            }
        } else {
            keys[i].last_raw = raw;
            keys[i].debounce_cnt = 0;
        }

        /*
         * 长按检测: 基于真实时间 (HAL_GetTick),
         * 不受主循环负载引起的扫描间隔波动影响。
         */
        if (keys[i].stable_state == 0 && !keys[i].long_press_fired) {
            if (now - keys[i].press_start_tick >= KEY_LONG_PRESS_MS) {
                keys[i].long_press_fired = 1;
                DEBUG_PRINTF("[KEY%d] Long Press", i + 1);
                keys[i].last_repeat_tick = now;
                keys[i].pending_event = KEY_EVENT_LONG_PRESS;
                keys[i].event_pending = 1;
            }
        }

        /*
         * 长按已触发后, 周期性产生 REPEAT 事件
         * 用于菜单快速滚动, 每 KEY_LONG_REPEAT_MS 一次
         */
        if (keys[i].stable_state == 0 && keys[i].long_press_fired) {
            if (now - keys[i].last_repeat_tick >= KEY_LONG_REPEAT_MS) {
                keys[i].last_repeat_tick = now;
                DEBUG_PRINTF("[KEY%d] Repeat", i + 1);
                keys[i].pending_event = KEY_EVENT_LONG_PRESS_REPEAT;
                keys[i].event_pending = 1;
            }
        }
    }
    return 0;
}

void key_drv_exti_callback(uint8_t key_id)
{
    if (key_id < 1 || key_id > KEY_COUNT) return;
}
