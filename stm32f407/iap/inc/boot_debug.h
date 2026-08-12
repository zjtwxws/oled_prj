/**
 * @file    boot_debug.h
 * @brief   Bootloader 共享调试输出 — 统一 printf / log 接口
 *
 * boot_main.c 提供 boot_printf() / boot_log_impl() 实现，
 * 其他模块（如 boot_fw_info.c）通过本头文件引用，避免重复实现。
 */

#ifndef __BOOT_DEBUG_H
#define __BOOT_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

/**
 * @brief  格式化输出到调试串口（huart2）
 * @param  fmt  格式化字符串
 * @param  ...  可变参数
 * @note   支持 %s %d %u %x %X %02X %08X %c %%
 */
void boot_printf(const char *fmt, ...);

/**
 * @brief  带源码位置的调试日志
 * @param  func  __func__ 调用者函数名
 * @param  line  __LINE__ 调用行号
 * @param  fmt   格式化字符串
 * @param  ...   可变参数
 */
void boot_log_impl(const char *func, int line, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* __BOOT_DEBUG_H */
