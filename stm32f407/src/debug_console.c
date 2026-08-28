/**
 * @file    debug_console.c
 * @brief   调试串口实现 — TX 输出 + CLI RX 中断 + nr_micro_shell 移植
 *
 * UART2: PA2(TX) + PA3(RX), 115200-8-N-1
 *   - TX: 阻塞 HAL_UART_Transmit (debug_printf/debug_hexdump/shell_putc)
 *   - RX: 中断接收 + 环形缓冲区 (cli_rx_buf)
 *
 * 与 RK3506 通信的 uart_drv (UART1) 完全独立。
 */

#include "debug_console.h"

#if DEBUG_UART_ENABLE

#include "stm32f4xx_hal.h"
#include "nr_micro_shell_port.h"
#include "nr_micro_shell.h"
#include "cli_cmds.h"
#include "sys_config.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* ---- 调试 UART 句柄 (UART2) ---- */
static UART_HandleTypeDef *p_debug_uart = NULL;

/* ---- CLI RX 环形缓冲区 ---- */
#define CLI_RX_BUF_SIZE    64
static uint8_t  cli_rx_buf[CLI_RX_BUF_SIZE];
static volatile uint16_t cli_rx_head = 0;
static volatile uint16_t cli_rx_tail = 0;
static uint8_t  cli_rx_byte;       /* ISR 接收临时存放 */

/* ---- shell_putc 实现 (nr_micro_shell 移植层必须) ---- */

/**
 * @brief  向调试串口输出一个字符（nr_micro_shell 移植层实现）
 * @param  c  要输出的字符
 * @note   直接使用 HAL 阻塞发送，与 shell_printf 内部调用路径一致
 */
void shell_putc(char c)
{
    if (p_debug_uart)
    {
        HAL_UART_Transmit(p_debug_uart, (uint8_t *)&c, 1, HAL_MAX_DELAY);
    }
}

/**
 * @brief  获取当前纳秒时间戳（用于 time 命令）
 * @return 基于 HAL_GetTick 的纳秒级近似值
 * @note   STM32F4 SysTick 精度为 1ms, 此处返回 ms * 1000000 仅为满足接口
 */
uint64_t shell_get_ts_ns(void)
{
    return (uint64_t)HAL_GetTick() * 1000000UL;
}

/* ---- 初始化 ---- */

/**
 * @brief  初始化调试串口 (UART2)
 * @param  huart  HAL UART 句柄 (&huart2)
 * @date   2026-08-07
 */
void debug_console_init(void *huart)
{
    p_debug_uart = (UART_HandleTypeDef *)huart;

    /* 上电打招呼 */
    debug_console_write("\r\n================================================\r\n");
    debug_console_write("  STM32F407 OLED Gateway - Debug Console Ready\r\n");
    debug_console_write("================================================\r\n\r\n");
}

/* ---- 原始字符串输出 ---- */

/**
 * @brief  调试串口输出原始字符串
 * @param  str  以 '\0' 结尾的字符串
 */
void debug_console_write(const char *str)
{
    if (p_debug_uart && str)
    {
        uint16_t len = (uint16_t)strlen(str);
        if (len > 0)
        {
            HAL_UART_Transmit(p_debug_uart, (uint8_t *)(uintptr_t)str, len, HAL_MAX_DELAY);
        }
    }
}

/* ---- 格式化输出 (自动追加 \r\n) ---- */

/**
 * @brief  调试串口格式化输出（自动追加 \r\n）
 * @param  fmt  格式化字符串
 * @param  ...  可变参数
 */
void debug_printf(const char *fmt, ...)
{
    if (p_debug_uart == NULL)
    {
        return;
    }

    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0 && (uint16_t)len < sizeof(buf))
    {
        HAL_UART_Transmit(p_debug_uart, (uint8_t *)buf, (uint16_t)len, HAL_MAX_DELAY);
    }
    HAL_UART_Transmit(p_debug_uart, (uint8_t *)"\r\n", 2, HAL_MAX_DELAY);
}

/* ---- 十六进制 dump ---- */

/**
 * @brief  调试串口十六进制 dump
 * @param  data  数据指针
 * @param  len   数据长度
 */
void debug_hexdump(const uint8_t *data, uint16_t len)
{
    if (p_debug_uart == NULL || data == NULL)
    {
        return;
    }

    for (uint16_t i = 0; i < len; i++)
    {
        char hex[4];
        snprintf(hex, sizeof(hex), "%02X ", data[i]);
        HAL_UART_Transmit(p_debug_uart, (uint8_t *)hex, 3, HAL_MAX_DELAY);

        if ((i + 1) % 16 == 0)
        {
            HAL_UART_Transmit(p_debug_uart, (uint8_t *)"\r\n", 2, HAL_MAX_DELAY);
        }
    }
    if (len % 16 != 0)
    {
        HAL_UART_Transmit(p_debug_uart, (uint8_t *)"\r\n", 2, HAL_MAX_DELAY);
    }
}

/* ================================================
 *  CLI 子系统 (基于 nr_micro_shell)
 * ================================================ */

/**
 * @brief  初始化 CLI（启动 UART2 中断接收 + shell 引擎）
 * @note   必须在 debug_console_init() 之后调用
 */
void cli_init(void)
{
    if (p_debug_uart == NULL)
    {
        return;
    }

    /* 初始化环形缓冲区 */
    cli_rx_head = 0;
    cli_rx_tail = 0;

    /* 启动 UART2 中断接收 */
    HAL_UART_Receive_IT(p_debug_uart, &cli_rx_byte, 1);

    /* 初始化 nr_micro_shell 引擎 */
    shell_init();

	/* 注册默认的 CLI 命令 */
    cli_cmds_init();
}

/**
 * @brief  UART2 接收中断回调 (由 HAL_UART_RxCpltCallback 分发)
 * @note   接收到的字节保存在 cli_rx_byte 中，由 HAL_UART_Receive_IT 填入
 * @note   写入环形缓冲区; ISR 上下文, 不做耗时操作
 */
void debug_console_rx_callback(void)
{
    uint16_t next = cli_rx_head + 1;
    if (((next - cli_rx_tail) & (CLI_RX_BUF_SIZE - 1)) != 0)
    {
        cli_rx_buf[cli_rx_head & (CLI_RX_BUF_SIZE - 1)] = cli_rx_byte;
        cli_rx_head = next;
    }

    /* 重新启动中断接收，等待下一个字节 */
    if (p_debug_uart)
    {
        HAL_StatusTypeDef ret = HAL_UART_Receive_IT(p_debug_uart, &cli_rx_byte, 1);
        if (ret != HAL_OK)
        {
            /* 失败时清错误标志后重试一次 */
            __HAL_UART_CLEAR_FLAG(p_debug_uart,
                                  UART_FLAG_ORE | UART_FLAG_NE |
                                  UART_FLAG_FE | UART_FLAG_PE);
            HAL_UART_Receive_IT(p_debug_uart, &cli_rx_byte, 1);
        }
    }
}

/**
 * @brief  CLI 主循环轮询 — 从环形缓冲区取字节喂给 shell 引擎
 * @note   在 while(1) 主循环中每帧调用
 */
void cli_poll(void)
{
    while (((cli_rx_head - cli_rx_tail) & (CLI_RX_BUF_SIZE - 1)) != 0)
    {
        uint8_t c = cli_rx_buf[cli_rx_tail & (CLI_RX_BUF_SIZE - 1)];
        cli_rx_tail++;
        shell(c);
    }
}

#endif /* DEBUG_UART_ENABLE */



