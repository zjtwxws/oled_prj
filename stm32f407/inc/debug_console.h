/**
 * @file    debug_console.h
 * @brief   调试串口输出模块
 *
 * 通过 DEBUG_UART_ENABLE 宏整体开关。
 * 设为 0 时所有调试代码编译为空, 无体积开销
 * (需同时从 Makefile 中移除 debug_console.c 以彻底剔除目标文件)。
 */

#ifndef __DEBUG_CONSOLE_H
#define __DEBUG_CONSOLE_H

#include <stdint.h>

/* 调试开关: 1=开启, 0=完全关闭 */
#define DEBUG_UART_ENABLE  0

#if DEBUG_UART_ENABLE

#include <stdio.h>
#include <stdarg.h>

void debug_console_init(void *huart);
void debug_console_write(const char *str);
void debug_printf(const char *fmt, ...);
void debug_hexdump(const uint8_t *data, uint16_t len);

#define DEBUG_PRINTF(fmt, ...)   debug_printf(fmt, ##__VA_ARGS__)
#define DEBUG_HEXDUMP(d, l)      debug_hexdump(d, l)
#define DEBUG_WRITE(str)         debug_console_write(str)

#else  /* !DEBUG_UART_ENABLE */

#define debug_console_init(h)    ((void)0)
#define DEBUG_PRINTF(fmt, ...)   ((void)0)
#define DEBUG_HEXDUMP(d, l)      ((void)0)
#define DEBUG_WRITE(str)         ((void)0)

#endif /* DEBUG_UART_ENABLE */

#endif /* __DEBUG_CONSOLE_H */
