/**
 * @file    debug_console.h
 * @brief   调试串口模块 — TX 输出 + CLI RX 中断接收 + nr_micro_shell 移植层
 *
 * 通过 DEBUG_UART_ENABLE 宏整体开关。
 * 设为 0 时所有调试代码编译为空, 无体积开销。
 */

#ifndef __DEBUG_CONSOLE_H
#define __DEBUG_CONSOLE_H

#include <stdint.h>

/* 调试开关: 1=开启, 0=完全关闭 */
#define DEBUG_UART_ENABLE  1

#if DEBUG_UART_ENABLE

#include <stdio.h>
#include <stdarg.h>

/* ---- UART 层接口 ---- */
void debug_console_init(void *huart);
void debug_console_write(const char *str);
void debug_printf(const char *fmt, ...);
void debug_hexdump(const uint8_t *data, uint16_t len);

/* ---- CLI 接口 (基于 nr_micro_shell) ---- */

/**
 * @brief  初始化 CLI shell（需先调用 debug_console_init）
 * @note   启动 UART2 中断接收，初始化 shell 引擎，注册命令
 */
void cli_init(void);

/**
 * @brief  CLI 主循环轮询（在 while(1) 中调用）
 * @note   每帧检查 CLI 环形缓冲区，喂字符给 shell 引擎
 */
void cli_poll(void);

/**
 * @brief  UART2 接收中断回调（由 HAL_UART_RxCpltCallback 分发调用）
 * @param  byte  接收到的字节
 * @note   写入 CLI 专用环形缓冲区
 */
void debug_console_rx_callback(void);

/* ---- 便捷宏 ---- */
#define DEBUG_PRINTF(fmt, ...)   debug_printf(fmt, ##__VA_ARGS__)
#define DEBUG_HEXDUMP(d, l)      debug_hexdump(d, l)
#define DEBUG_WRITE(str)         debug_console_write(str)

#else  /* !DEBUG_UART_ENABLE */

#define debug_console_init(h)       ((void)0)
#define DEBUG_PRINTF(fmt, ...)      ((void)0)
#define DEBUG_HEXDUMP(d, l)         ((void)0)
#define DEBUG_WRITE(str)            ((void)0)
#define cli_init()                  ((void)0)
#define cli_poll()                  ((void)0)
#define debug_console_rx_callback()     ((void)0)

#endif /* DEBUG_UART_ENABLE */

#endif /* __DEBUG_CONSOLE_H */


