/**
 * @file    uart_adapter.h
 * @brief   RK3506 端串口适配器 — Linux 串口封装 + 协议收发 + ACK/重传 (纯 C)
 */

#ifndef UART_ADAPTER_H
#define UART_ADAPTER_H

#include "protocol.h"
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>

#define UART_RETRY_MAX      3
#define UART_TIMEOUT_MS     500
#define UART_PENDING_MAX    16
#define UART_TXQUEUE_MAX    64

/* 帧回调函数指针 */
typedef void (*uart_frame_callback_t)(const ProtoFrame* frame, void* user_data);

/* 待确认命令 */
typedef struct {
    uint8_t cmd;
    uint8_t seq;
    int     retry;
    uint8_t data[PROTO_MAX_DATA];
    uint8_t data_len;
    struct timeval sent;
    int     active;
} PendingCmd;

/* 串口适配器 */
typedef struct {
    int         fd;
    int         running;
    uint8_t     seq;

    uart_frame_callback_t frame_cb;
    void*       frame_cb_user_data;

    ProtoParser proto;

    /* ACK 等待队列 */
    PendingCmd  pending[UART_PENDING_MAX];
    int         pending_count;
    pthread_mutex_t pending_mutex;

    /* 待发送队列 */
    uint8_t     tx_queue[UART_TXQUEUE_MAX][PROTO_FRAME_MAX];
    uint16_t    tx_queue_len[UART_TXQUEUE_MAX];
    int         tx_queue_head;
    int         tx_queue_tail;
    int         tx_queue_count;
    pthread_mutex_t tx_mutex;

    pthread_t   rx_thread;
} UartAdapter;

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化/销毁 */
void uart_init(UartAdapter* ua);
void uart_deinit(UartAdapter* ua);

/* 打开/关闭串口 */
int  uart_open(UartAdapter* ua, const char* device, int baud);
void uart_close(UartAdapter* ua);
int  uart_is_open(const UartAdapter* ua);

/* 设置收到 STM32 帧的回调 */
void uart_on_frame(UartAdapter* ua, uart_frame_callback_t cb, void* user_data);

/* 发送指令 (带 ACK 等待), 返回 seq 号 */
int  uart_send_command(UartAdapter* ua, uint8_t cmd, const uint8_t* data,
                       uint8_t len, uint8_t expected_ack_cmd, int timeout_ms);

/* 发送原始帧 (不等待 ACK) */
void uart_send_raw(UartAdapter* ua, uint8_t cmd, uint8_t seq,
                   const uint8_t* data, uint8_t len);

/* 事件轮询 (需在主循环中调用) */
void uart_poll(UartAdapter* ua);

#ifdef __cplusplus
}
#endif

#endif /* UART_ADAPTER_H */
