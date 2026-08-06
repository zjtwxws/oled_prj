/**
 * @file    uart_drv.h
 * @brief   UART 驱动抽象层 (中断接收 + DMA 发送 + 环形缓冲区 + 错误恢复)
 */

#ifndef __UART_DRV_H
#define __UART_DRV_H

#include <stdint.h>

#define UART_RX_BUF_SIZE    512    /* 环形接收缓冲区大小 (必须为 2 的幂，用于掩码取模) */

/** @brief 初始化 UART (启动中断接收) */
void uart_drv_init(void *huart);
/** @brief 阻塞发送数据 */
int uart_drv_send(const uint8_t *data, uint16_t len);
/** @brief DMA 发送数据 (非阻塞) */
int uart_drv_send_dma(const uint8_t *data, uint16_t len);
/** @brief 查询发送是否忙 (DMA 模式下检查状态) */
int uart_drv_tx_busy(void);
/** @brief 返回环形缓冲区中可读字节数 */
uint16_t uart_drv_available(void);
/** @brief 从环形缓冲区读取一个字节，返回 1=成功, 0=无数据 */
int uart_drv_read_byte(uint8_t *ch);
/** @brief 批量读取环形缓冲区数据，返回实际读取字节数 */
uint16_t uart_drv_read(uint8_t *buf, uint16_t max_len);
/** @brief 清空接收缓冲区 */
void uart_drv_flush_rx(void);
/** @brief 接收中断回调: 将收到的字节写入环形缓冲区并重新启动接收 */
void uart_drv_rx_callback(uint8_t byte);

#endif
