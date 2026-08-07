/**
 * @file    key_drv.h
 * @brief   按键驱动 (消抖 + 长按检测)
 *
 * 实际接线:
 *   KEY1: PE1, KEY2: PE2, KEY3: PE3, KEY4: PE4 (全部上拉输入, 按下=低电平)
 *
 * 接口: STM32 HAL GPIO, 在 CubeMX 中配置引脚为 GPIO_Input 上拉模式。
 */

#ifndef __KEY_DRV_H
#define __KEY_DRV_H

#include <stdint.h>

#define KEY_DEBOUNCE_MS    20     /* 消抖时间 (ms)，连续采样确认稳定后才判定按下 */
#define KEY_LONG_PRESS_MS  1000  /* 长按判定阈值 (ms)，超过此时间触发长按事件 */
#define KEY_LONG_REPEAT_MS  150   /* 长按重复间隔 (ms)，长按保持期间周期性产生 REPEAT 事件 */

typedef enum
{
    KEY_EVENT_NONE        = 0,        /* 无事件 */
    KEY_EVENT_SHORT_PRESS = 1,   /* 短按 (按下后释放，时长 < 长按阈值) */
    KEY_EVENT_LONG_PRESS  = 2,    /* 长按 (按住达到 KEY_LONG_PRESS_MS 时触发一次) */
    KEY_EVENT_RELEASE     = 3,      /* 释放 (长按后松开产生，短按不产生此事件) */
    KEY_EVENT_LONG_PRESS_REPEAT = 4  /* 长按重复 (长按保持期间每 KEY_LONG_REPEAT_MS 产生一次) */
} key_event_t;

typedef struct
{
    uint8_t key_id;     /* 按键编号 (1=KEY1, 2=KEY2, 3=KEY3, 4=KEY4) */
    key_event_t event;  /* 按键事件类型 */
} key_info_t;

/*
 * 端口配置宏 — 使用前需确保 CubeMX 已生成对应的 GPIO 初始化代码。
 */
#define KEY_PORT1   GPIOE     /* KEY1 端口 */
#define KEY_PIN1    GPIO_PIN_1  /* KEY1 引脚: PE1 */
#define KEY_PORT2   GPIOE     /* KEY2 端口 */
#define KEY_PIN2    GPIO_PIN_2  /* KEY2 引脚: PE2 */
#define KEY_PORT3   GPIOE     /* KEY3 端口 */
#define KEY_PIN3    GPIO_PIN_3  /* KEY3 引脚: PE3 */
#define KEY_PORT4   GPIOE     /* KEY4 端口 */
#define KEY_PIN4    GPIO_PIN_4  /* KEY4 引脚: PE4 */

#define KEY_COUNT   4  /* 启用全部 4 个按键 */

/** @brief 初始化按键驱动 (配置 GPIO 读取引脚映射) */
void key_drv_init(void);
/** @brief 扫描所有按键 (消抖+长按检测)，返回 1=有事件待处理，事件写入 info */
int key_drv_scan(key_info_t *info);
/** @brief 外部中断回调 (预留，当前使用轮询模式) */
void key_drv_exti_callback(uint8_t key_id);

#endif
