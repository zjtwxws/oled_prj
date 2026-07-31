/**
 * @file    sys_tick.c
 * @brief   系统滴答时钟实现 (调用 HAL_GetTick)
 */

#include "sys_tick.h"
#include "stm32f4xx_hal.h"

uint32_t sys_tick_ms(void)
{
    return HAL_GetTick();
}
