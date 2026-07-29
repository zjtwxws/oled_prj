/**
 * @file    uart_drv.c
 * @brief   UART 驱动实现
 *
 * 环形缓冲区 + 中断接收 (每次接收 1 字节, HAL_UART_Receive_IT)
 * 发送: 阻塞 (HAL_UART_Transmit) 或 DMA (HAL_UART_Transmit_DMA)
 */

#include "uart_drv.h"
#include "stm32f4xx_hal.h"
#include <string.h>

static UART_HandleTypeDef *p_uart = NULL;

/* 环形接收缓冲区 */
static uint8_t  rx_buf[UART_RX_BUF_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;   /* ISR 读, 主循环写 → 需 volatile */
static uint8_t  rx_byte;  /* 中断接收单字节缓冲 */

void uart_drv_init(void *huart)
{
    p_uart = (UART_HandleTypeDef *)huart;
    rx_head = 0;
    rx_tail = 0;

    /* 启动单字节中断接收 */
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
    /* else: 缓冲区满, 丢弃字节 */

    /* 重新启动中断接收 */
    if (p_uart) {
        HAL_UART_Receive_IT(p_uart, &rx_byte, 1);
    }
}

/*
 * HAL UART 接收完成回调。
 * HAL 中断流程: USART_IRQHandler → HAL_UART_IRQHandler 读取 DR 到 rx_byte
 *               → 调用 HAL_UART_RxCpltCallback。
 * 此时 rx_byte 已包含有效字节, 直接传给环形缓冲区即可, 切勿再次读 DR。
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (p_uart && huart->Instance == p_uart->Instance) {
        uart_drv_rx_callback(rx_byte);
    }
}
