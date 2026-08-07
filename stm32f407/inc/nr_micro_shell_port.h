/**
 * @file    nr_micro_shell_port.h
 * @brief   nr_micro_shell 移植层 — 对接 debug_console 输出
 *
 * 本文件提供 shell_putc() 实现，将 shell 输出通过 debug_console_write() 发送。
 */

#ifndef __NR_MICRO_SHELL_PORT_H
#define __NR_MICRO_SHELL_PORT_H

#include <stdint.h>

/* 使能历史命令功能 */
#define NR_SHELL_HISTORY_CMD_SUPPORT
#define NR_SHELL_HISTORY_CMD_NUM    8
#define NR_SHELL_HISTORY_CMD_SZ     64

/* 使能 Tab 自动补全 */
#define NR_SHELL_AUTO_COMPLETE_SUPPORT

/* 命令行最大长度 */
#define NR_SHELL_MAX_LINE_SZ        80

/* 最大参数个数 */
#define NR_SHELL_MAX_PARAM_NUM      8

/* Shell 提示符 */
#define NR_SHELL_PROMPT             "oled"

/* 显示启动 Logo */
#define NR_SHELL_SHOW_LOGO

/* ========== 必须实现的接口 ========== */

/**
 * @brief  shell 输出单个字符（移植层必须实现）
 * @param  c  要输出的字符
 */
void shell_putc(char c);

/**
 * @brief  获取当前纳秒时间戳（用于 time 命令，可选）
 * @return 纳秒级时间戳
 */
uint64_t shell_get_ts_ns(void);

#endif /* __NR_MICRO_SHELL_PORT_H */
