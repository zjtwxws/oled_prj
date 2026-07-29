/**
 * @file    uart_adapter.c
 * @brief   RK3506 端串口适配器实现 (纯 C)
 */

#include "uart_adapter.h"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- 内部辅助 ---------- */

static long timeval_diff_ms(const struct timeval* start, const struct timeval* end)
{
    long sec  = end->tv_sec  - start->tv_sec;
    long usec = end->tv_usec - start->tv_usec;
    return sec * 1000 + usec / 1000;
}

static void process_rx(UartAdapter* ua, const ProtoFrame* frame)
{
    /* ACK/NAK 处理 */
    if (frame->cmd == CMD_ACK || frame->cmd == CMD_NAK) {
        pthread_mutex_lock(&ua->pending_mutex);
        int i;
        for (i = 0; i < ua->pending_count; i++) {
            if (ua->pending[i].active && ua->pending[i].seq == frame->seq) {
                ua->pending[i].active = 0;
                break;
            }
        }
        /* 压缩 pending 数组 */
        int j = 0;
        for (i = 0; i < ua->pending_count; i++) {
            if (ua->pending[i].active) {
                if (j != i) ua->pending[j] = ua->pending[i];
                j++;
            }
        }
        ua->pending_count = j;
        pthread_mutex_unlock(&ua->pending_mutex);
    }

    /* 回调上层 */
    if (ua->frame_cb) {
        ua->frame_cb(frame, ua->frame_cb_user_data);
    }
}

static void* read_thread_func(void* arg)
{
    UartAdapter* ua = (UartAdapter*)arg;
    uint8_t buf[256];

    while (ua->running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(ua->fd, &fds);
        struct timeval tv = {0, 50000}; /* 50ms */
        int ret = select(ua->fd + 1, &fds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(ua->fd, &fds)) {
            ssize_t n = read(ua->fd, buf, sizeof(buf));
            ssize_t i;
            for (i = 0; i < n; i++) {
                if (proto_feed(&ua->proto, buf[i])) {
                    process_rx(ua, proto_get_frame(&ua->proto));
                }
            }
        }
    }
    return NULL;
}

/* ---------- 公共接口 ---------- */

void uart_init(UartAdapter* ua)
{
    memset(ua, 0, sizeof(*ua));
    ua->fd = -1;
    ua->running = 0;
    ua->seq = 0;
    proto_init(&ua->proto);
    pthread_mutex_init(&ua->pending_mutex, NULL);
    pthread_mutex_init(&ua->tx_mutex, NULL);
}

void uart_deinit(UartAdapter* ua)
{
    uart_close(ua);
    pthread_mutex_destroy(&ua->pending_mutex);
    pthread_mutex_destroy(&ua->tx_mutex);
}

int uart_open(UartAdapter* ua, const char* device, int baud)
{
    (void)baud;
    ua->fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (ua->fd < 0) return -1;

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);
    tty.c_cflag = CS8 | CREAD | CLOCAL;
    tty.c_iflag = IGNPAR;
    tty.c_oflag = 0;
    tty.c_lflag = 0;
    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 1;
    tcflush(ua->fd, TCIFLUSH);
    tcsetattr(ua->fd, TCSANOW, &tty);

    ua->running = 1;
    pthread_create(&ua->rx_thread, NULL, read_thread_func, ua);
    return 0;
}

void uart_close(UartAdapter* ua)
{
    ua->running = 0;
    if (ua->rx_thread) {
        pthread_join(ua->rx_thread, NULL);
        memset(&ua->rx_thread, 0, sizeof(ua->rx_thread));
    }
    if (ua->fd >= 0) {
        close(ua->fd);
        ua->fd = -1;
    }
}

int uart_is_open(const UartAdapter* ua)
{
    return ua->fd >= 0 && ua->running;
}

void uart_on_frame(UartAdapter* ua, uart_frame_callback_t cb, void* user_data)
{
    ua->frame_cb = cb;
    ua->frame_cb_user_data = user_data;
}

int uart_send_command(UartAdapter* ua, uint8_t cmd, const uint8_t* data,
                       uint8_t len, uint8_t expected_ack_cmd, int timeout_ms)
{
    (void)expected_ack_cmd;
    (void)timeout_ms;

    uint8_t seq = ua->seq++;
    uart_send_raw(ua, cmd, seq, data, len);

    pthread_mutex_lock(&ua->pending_mutex);
    if (ua->pending_count < UART_PENDING_MAX) {
        PendingCmd* pc = &ua->pending[ua->pending_count++];
        pc->cmd   = cmd;
        pc->seq   = seq;
        pc->retry = 0;
        pc->data_len = (len > PROTO_MAX_DATA) ? PROTO_MAX_DATA : len;
        if (len > 0 && data) {
            memcpy(pc->data, data, pc->data_len);
        }
        gettimeofday(&pc->sent, NULL);
        pc->active = 1;
    }
    pthread_mutex_unlock(&ua->pending_mutex);

    return seq;
}

void uart_send_raw(UartAdapter* ua, uint8_t cmd, uint8_t seq,
                   const uint8_t* data, uint8_t len)
{
    uint16_t frame_len = proto_build(&ua->proto, cmd, seq, data, len);
    const uint8_t* buf = proto_tx_buf(&ua->proto);

    pthread_mutex_lock(&ua->tx_mutex);
    if (ua->tx_queue_count < UART_TXQUEUE_MAX) {
        int idx = ua->tx_queue_tail;
        memcpy(ua->tx_queue[idx], buf, frame_len);
        ua->tx_queue_len[idx] = frame_len;
        ua->tx_queue_tail = (idx + 1) % UART_TXQUEUE_MAX;
        ua->tx_queue_count++;
    }
    pthread_mutex_unlock(&ua->tx_mutex);
}

void uart_poll(UartAdapter* ua)
{
    /* 发送队列 */
    pthread_mutex_lock(&ua->tx_mutex);
    while (ua->tx_queue_count > 0) {
        int idx = ua->tx_queue_head;
        if (ua->fd >= 0) {
            write(ua->fd, ua->tx_queue[idx], ua->tx_queue_len[idx]);
        }
        ua->tx_queue_head = (idx + 1) % UART_TXQUEUE_MAX;
        ua->tx_queue_count--;
    }
    pthread_mutex_unlock(&ua->tx_mutex);

    /* ACK 超时重传 */
    pthread_mutex_lock(&ua->pending_mutex);
    {
        struct timeval now;
        gettimeofday(&now, NULL);
        int i;
        for (i = 0; i < ua->pending_count; i++) {
            PendingCmd* pc = &ua->pending[i];
            if (!pc->active) continue;

            long elapsed = timeval_diff_ms(&pc->sent, &now);
            if (elapsed > UART_TIMEOUT_MS) {
                if (pc->retry < UART_RETRY_MAX) {
                    pc->retry++;
                    pc->sent = now;
                    /* 重发 */
                    uint16_t flen = proto_build(&ua->proto, pc->cmd, pc->seq,
                                                 pc->data, pc->data_len);
                    if (ua->fd >= 0) {
                        write(ua->fd, proto_tx_buf(&ua->proto), flen);
                    }
                    fprintf(stderr, "[UART] Retry cmd=0x%02X seq=%d (%d/%d)\n",
                            pc->cmd, pc->seq, pc->retry, UART_RETRY_MAX);
                } else {
                    fprintf(stderr, "[UART] Command 0x%02X seq=%d timeout after %d retries\n",
                            pc->cmd, pc->seq, UART_RETRY_MAX);
                    pc->active = 0;
                }
            }
        }

        /* 压缩 pending 数组 */
        int j = 0;
        for (i = 0; i < ua->pending_count; i++) {
            if (ua->pending[i].active) {
                if (j != i) ua->pending[j] = ua->pending[i];
                j++;
            }
        }
        ua->pending_count = j;
    }
    pthread_mutex_unlock(&ua->pending_mutex);
}
