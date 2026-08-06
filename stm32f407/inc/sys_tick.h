/**
 * @file    sys_tick.h
 * @brief   系统滴答时钟抽象层 (封装 HAL_GetTick)
 */

#ifndef __SYS_TICK_H
#define __SYS_TICK_H

#include <stdint.h>

/** @brief 获取系统运行毫秒数 (封装 HAL_GetTick) */
uint32_t sys_tick_ms(void);

#endif
