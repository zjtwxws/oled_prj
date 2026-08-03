/**
 * @file    debug_console.c
 * @brief   调试串口实现 (HAL 阻塞发送)
 *
 * 调试串口仅用于 TX (发送), 使用 HAL_USART_Transmit 阻塞模式.
 * 与 RK3506 通信的 uart_drv 完全独立.
 * 整个文件内容受 DEBUG_UART_ENABLE 宏控制, 关闭时不编译.
 */

#include "debug_console.h"

#if DEBUG_UART_ENABLE

#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static UART_HandleTypeDef *p_debug_uart = NULL;

/* ---- 初始化 ---- */

void debug_console_init(void *huart)
{
    p_debug_uart = (UART_HandleTypeDef *)huart;

    /* 上电打招呼 */
    debug_console_write("\r\n================================================\r\n");
    debug_console_write("  STM32F407 OLED Gateway - Debug Console Ready\r\n");
    debug_console_write("================================================\r\n\r\n");
}

/* ---- 原始字符串输出 ---- */

void debug_console_write(const char *str)
{
    if (p_debug_uart && str) {
        uint16_t len = (uint16_t)strlen(str);
        if (len > 0) {
            HAL_UART_Transmit(p_debug_uart, (uint8_t *)str, len, HAL_MAX_DELAY);
        }
    }
}

/* ---- 格式化输出 (自动追加 \r\n) ---- */

void debug_printf(const char *fmt, ...)
{
    if (p_debug_uart == NULL) return;

    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0 && (uint16_t)len < sizeof(buf)) {
        HAL_UART_Transmit(p_debug_uart, (uint8_t *)buf, (uint16_t)len, HAL_MAX_DELAY);
    }
    /* 追加换行 */
    HAL_UART_Transmit(p_debug_uart, (uint8_t *)"\r\n", 2, HAL_MAX_DELAY);
}

/* ---- 十六进制 dump ---- */

void debug_hexdump(const uint8_t *data, uint16_t len)
{
    if (p_debug_uart == NULL || data == NULL) return;

    for (uint16_t i = 0; i < len; i++) {
        char hex[4];
        snprintf(hex, sizeof(hex), "%02X ", data[i]);
        HAL_UART_Transmit(p_debug_uart, (uint8_t *)hex, 3, HAL_MAX_DELAY);

        /* 每 16 字节换行 */
        if ((i + 1) % 16 == 0) {
            HAL_UART_Transmit(p_debug_uart, (uint8_t *)"\r\n", 2, HAL_MAX_DELAY);
        }
    }
    if (len % 16 != 0) {
        HAL_UART_Transmit(p_debug_uart, (uint8_t *)"\r\n", 2, HAL_MAX_DELAY);
    }
}

#endif /* DEBUG_UART_ENABLE */
