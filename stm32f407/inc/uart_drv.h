/**
 * @file    uart_drv.h
 * @brief   UART 驱动抽象层 (中断接收 + DMA 发送 + 环形缓冲区)
 */

#ifndef __UART_DRV_H
#define __UART_DRV_H

#include <stdint.h>

#define UART_RX_BUF_SIZE    512    /* 环形缓冲区大小 */

/**
 * @brief  UART 硬件初始化
 * @param  huart  HAL UART 句柄指针
 */
void uart_drv_init(void *huart);

/**
 * @brief 发送数据 (阻塞, 使用 HAL UART Transmit)
 * @return 0=成功
 */
int uart_drv_send(const uint8_t *data, uint16_t len);

/**
 * @brief 发送数据 (DMA, 非阻塞)
 */
int uart_drv_send_dma(const uint8_t *data, uint16_t len);

/**
 * @brief 检查发送是否忙
 */
int uart_drv_tx_busy(void);

/**
 * @brief 从环形缓冲区读取字节数
 * @return 实际可用字节数
 */
uint16_t uart_drv_available(void);

/**
 * @brief 读取一个字节 (非阻塞)
 * @param  ch  输出指针
 * @return 1=读取成功, 0=缓冲区空
 */
int uart_drv_read_byte(uint8_t *ch);

/**
 * @brief 读取多个字节
 * @return 实际读取字节数
 */
uint16_t uart_drv_read(uint8_t *buf, uint16_t max_len);

/**
 * @brief 清空接收缓冲区
 */
void uart_drv_flush_rx(void);

/**
 * @brief UART 接收中断回调 (由 HAL_UART_RxCpltCallback 调用)
 */
void uart_drv_rx_callback(uint8_t byte);

#endif /* __UART_DRV_H */
