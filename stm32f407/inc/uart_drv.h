/**
 * @file    uart_drv.h
 * @brief   UART 驱动抽象层 (中断接收 + DMA 发送 + 环形缓冲区 + 错误恢复)
 */

#ifndef __UART_DRV_H
#define __UART_DRV_H

#include <stdint.h>

#define UART_RX_BUF_SIZE    512

void uart_drv_init(void *huart);
int uart_drv_send(const uint8_t *data, uint16_t len);
int uart_drv_send_dma(const uint8_t *data, uint16_t len);
int uart_drv_tx_busy(void);
uint16_t uart_drv_available(void);
int uart_drv_read_byte(uint8_t *ch);
uint16_t uart_drv_read(uint8_t *buf, uint16_t max_len);
void uart_drv_flush_rx(void);
void uart_drv_rx_callback(uint8_t byte);

#endif
