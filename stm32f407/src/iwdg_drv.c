/**
 * @file    iwdg_drv.c
 * @brief   独立看门狗实现
 */

#include "iwdg_drv.h"

#ifdef IWDG_ENABLE

#include "stm32f4xx_hal.h"

/* IWDG 句柄 */
static IWDG_HandleTypeDef hiwdg;

void iwdg_drv_init(void)
{
    /*
     * IWDG 时钟 = LSI (32 kHz)
     * 预分频器 = 256 → 计数器时钟 = 32000/256 = 125 Hz → 8 ms/tick
     * 重载值 = 125 → 超时 = 125 * 8 = 1000 ms (约 1s)
     */
    hiwdg.Instance       = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
    hiwdg.Init.Reload    = 125;
    /* IWDG 窗口值 = 4095 (最大, 不使能窗口) */
    hiwdg.Init.Window    = 4095;

    if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
    {
        /* 初始化失败, 进入错误处理 */
        while(1);
    }
}

void iwdg_drv_feed(void)
{
    HAL_IWDG_Refresh(&hiwdg);
}

#else
typedef int iwdg_dummy_t;  /* 空编译单元占位 */
#endif /* IWDG_ENABLE */
