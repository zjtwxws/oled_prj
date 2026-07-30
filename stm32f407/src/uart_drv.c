/**
 * @file    uart_drv.c
 * @brief   UART 驱动实现 (中断接收 + 环形缓冲区)
 */

#include "uart_drv.h"
#include "stm32f4xx_hal.h"
#include <string.h>

static UART_HandleTypeDef *p_uart = NULL;

static uint8_t  rx_buf[UART_RX_BUF_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;
static uint8_t  rx_byte;

void uart_drv_init(void *huart)
{
    p_uart = (UART_HandleTypeDef *)huart;
    rx_head = 0;
    rx_tail = 0;

    HAL_UART_Receive_IT(p_uart, &rx_byte, 1);
}

int uart_drv_send(const uint8_t *data, uint16_t len)
{
    if (p_uart == NULL) return -1;
    HAL_StatusTypeDef status = HAL_UART_Transmit(p_uart, (uint8_t *)data, len, HAL_MAX_DELAY);
    return (status == HAL_OK) ? 0 : -1;
}

int uart_drv_send_dma(const uint8_t *data, uint16_t len)
{
    if (p_uart == NULL) return -1;
    HAL_StatusTypeDef status = HAL_UART_Transmit_DMA(p_uart, (uint8_t *)data, len);
    return (status == HAL_OK) ? 0 : -1;
}

int uart_drv_tx_busy(void)
{
    if (p_uart == NULL) return 0;
    return (p_uart->gState != HAL_UART_STATE_READY) ? 1 : 0;
}

uint16_t uart_drv_available(void)
{
    return (rx_head - rx_tail) & (UART_RX_BUF_SIZE - 1);
}

int uart_drv_read_byte(uint8_t *ch)
{
    if (rx_head == rx_tail) return 0;
    *ch = rx_buf[rx_tail & (UART_RX_BUF_SIZE - 1)];
    rx_tail++;
    return 1;
}

uint16_t uart_drv_read(uint8_t *buf, uint16_t max_len)
{
    uint16_t i;
    for (i = 0; i < max_len; i++) {
        if (rx_head == rx_tail) break;
        buf[i] = rx_buf[rx_tail & (UART_RX_BUF_SIZE - 1)];
        rx_tail++;
    }
    return i;
}

void uart_drv_flush_rx(void)
{
    rx_tail = rx_head;
}

void uart_drv_rx_callback(uint8_t byte)
{
    uint16_t next = (rx_head + 1) & (UART_RX_BUF_SIZE - 1);
    if (next != (rx_tail & (UART_RX_BUF_SIZE - 1))) {
        rx_buf[rx_head & (UART_RX_BUF_SIZE - 1)] = byte;
        rx_head = next;
    }

    /*
     * 重新启动中断接收。
     * 若 HAL_UART_Receive_IT 失败 (状态冲突/错误未清除),
     * 记录错误并尝试恢复: 调用 HAL_UART_AbortReceive_IT 清状态后重试。
     */
    if (p_uart) {
        HAL_StatusTypeDef ret = HAL_UART_Receive_IT(p_uart, &rx_byte, 1);
        if (ret != HAL_OK) {
            /* 错误恢复: 中止当前接收, 重新启动 */
            HAL_UART_AbortReceive_IT(p_uart);
            HAL_UART_Receive_IT(p_uart, &rx_byte, 1);
        }
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (p_uart && huart->Instance == p_uart->Instance) {
        uart_drv_rx_callback(rx_byte);
    }
}
