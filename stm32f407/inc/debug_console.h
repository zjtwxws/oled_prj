/**
 * @file    debug_console.h
 * @brief   调试串口输出模块 (独立于 RK3506 通信串口)
 *
 * 使用独立的 UART (如 USART2) 输出调试信息, 仅 TX, 无需环形缓冲.
 * 通过 DEBUG_UART_ENABLE 宏整体开关, 关闭时所有调试宏编译为空.
 *
 * 使用前需在 CubeMX 中配置调试串口并生成 MX_USARTx_UART_Init().
 */

#ifndef __DEBUG_CONSOLE_H
#define __DEBUG_CONSOLE_H

#include <stdint.h>

/* ================================================================
 *  调试开关: 1=开启调试输出, 0=完全关闭 (无代码体积开销)
 * ================================================================ */
#define DEBUG_UART_ENABLE  0

/* ================================================================
 *  公开接口
 * ================================================================ */

#if DEBUG_UART_ENABLE

#include <stdio.h>
#include <stdarg.h>

/**
 * @brief 初始化调试串口
 * @param huart  HAL UART 句柄指针 (如 &huart2)
 */
void debug_console_init(void *huart);

/**
 * @brief 发送原始字符串 (无格式化)
 * @param str  以 '\0' 结尾的字符串
 */
void debug_console_write(const char *str);

/**
 * @brief 格式化调试输出 (自动追加 \r\n)
 * @param fmt  格式化字符串
 */
void debug_printf(const char *fmt, ...);

/**
 * @brief 十六进制 dump 数据
 * @param data  数据指针
 * @param len   数据长度
 */
void debug_hexdump(const uint8_t *data, uint16_t len);

/* ---- 用户使用的调试宏 (自动追加换行) ---- */
#define DEBUG_PRINTF(fmt, ...)   debug_printf(fmt, ##__VA_ARGS__)
#define DEBUG_HEXDUMP(d, l)      debug_hexdump(d, l)
#define DEBUG_WRITE(str)         debug_console_write(str)

#else  /* !DEBUG_UART_ENABLE */

/* 关闭调试: 所有宏编译为空 */
#define debug_console_init(h)    ((void)0)
#define DEBUG_PRINTF(fmt, ...)   ((void)0)
#define DEBUG_HEXDUMP(d, l)      ((void)0)
#define DEBUG_WRITE(str)         ((void)0)

#endif /* DEBUG_UART_ENABLE */

#endif /* __DEBUG_CONSOLE_H */
