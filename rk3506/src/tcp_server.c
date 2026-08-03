/**
 * @file    tcp_server.c
 * @brief   TCP 服务器实现 (纯 C)
 */

#include "tcp_server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define TCP_READ_ERR_MAX  8  /* 连续读错误超过此值则断开连接 */

/* --- 服务线程 --- */

static void* tcp_thread_func(void* arg)
{
    TcpServer* ts = (TcpServer*)arg;
    while (ts->running) {
        tcp_poll(ts);
        usleep(10000); /* 10ms */
    }
    return NULL;
}

/* --- 公共接口 --- */

void tcp_init(TcpServer* ts)
{
    memset(ts, 0, sizeof(*ts));
    ts->listen_fd = -1;
    ts->port = 9527;
    pthread_mutex_init(&ts->client_mutex, NULL);
}

void tcp_deinit(TcpServer* ts)
{
    tcp_stop(ts);
    pthread_mutex_destroy(&ts->client_mutex);
}

int tcp_start(TcpServer* ts, int port)
{
    ts->port = (port > 0) ? port : 9527;
    ts->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ts->listen_fd < 0) return -1;

    int opt = 1;
    setsockopt(ts->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    fcntl(ts->listen_fd, F_SETFL, O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(ts->port);

    if (bind(ts->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(ts->listen_fd);
        ts->listen_fd = -1;
        return -2;
    }
    listen(ts->listen_fd, 5);

    ts->running = 1;
    pthread_create(&ts->thread, NULL, tcp_thread_func, ts);
    printf("[TcpServer] Listening on port %d\n", ts->port);
    return 0;
}

void tcp_stop(TcpServer* ts)
{
    ts->running = 0;
    if (ts->thread) {
        pthread_join(ts->thread, NULL);
        memset(&ts->thread, 0, sizeof(ts->thread));
    }

    pthread_mutex_lock(&ts->client_mutex);
    int i;
    for (i = 0; i < ts->client_count; i++) {
        if (ts->clients[i].active) {
            close(ts->clients[i].fd);
        }
    }
    ts->client_count = 0;
    pthread_mutex_unlock(&ts->client_mutex);

    if (ts->listen_fd >= 0) {
        close(ts->listen_fd);
        ts->listen_fd = -1;
    }
}

int tcp_is_running(const TcpServer* ts)
{
    return ts->running;
}

void tcp_on_message(TcpServer* ts, tcp_msg_callback_t cb, void* user_data)
{
    ts->msg_cb = cb;
    ts->msg_cb_user_data = user_data;
}

void tcp_send(TcpServer* ts, int client_id, const char* msg)
{
    pthread_mutex_lock(&ts->client_mutex);
    int i;
    for (i = 0; i < ts->client_count; i++) {
        if (ts->clients[i].active && ts->clients[i].fd == client_id) {
            write(ts->clients[i].fd, msg, strlen(msg));
            break;
        }
    }
    pthread_mutex_unlock(&ts->client_mutex);
}

void tcp_broadcast(TcpServer* ts, const char* msg)
{
    pthread_mutex_lock(&ts->client_mutex);
    int i;
    for (i = 0; i < ts->client_count; i++) {
        if (ts->clients[i].active) {
            write(ts->clients[i].fd, msg, strlen(msg));
        }
    }
    pthread_mutex_unlock(&ts->client_mutex);
}

void tcp_poll(TcpServer* ts)
{
    /* 接受新连接 */
    if (ts->listen_fd >= 0) {
        struct sockaddr_in cli;
        socklen_t len = sizeof(cli);
        int fd = accept(ts->listen_fd, (struct sockaddr*)&cli, &len);
        if (fd >= 0) {
            fcntl(fd, F_SETFL, O_NONBLOCK);
            pthread_mutex_lock(&ts->client_mutex);
            if (ts->client_count < TCP_CLIENT_MAX) {
                int idx = ts->client_count++;
                ts->clients[idx].fd = fd;
                ts->clients[idx].buffer[0] = '\0';
                ts->clients[idx].active = 1;
                ts->clients[idx].read_err_cnt = 0;
                printf("[TcpServer] Client connected: fd=%d\n", fd);
            } else {
                close(fd); /* 队列满，拒绝 */
            }
            pthread_mutex_unlock(&ts->client_mutex);
        }
    }

    /* 读取客户端数据 */
    char buf[1024];
    pthread_mutex_lock(&ts->client_mutex);
    int i;
    for (i = 0; i < ts->client_count; ) {
        TcpClient* cl = &ts->clients[i];
        if (!cl->active) { i++; continue; }

        ssize_t n = read(cl->fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            cl->read_err_cnt = 0;
            buf[n] = '\0';

            /* 追加到 buffer */
            int buf_used = (int)strlen(cl->buffer);
            int remaining = (int)(sizeof(cl->buffer) - buf_used - 1);
            if ((int)n > remaining) n = remaining;
            memcpy(cl->buffer + buf_used, buf, n);
            cl->buffer[buf_used + n] = '\0';

            /* 按 '\n' 分割消息 */
            char* ptr;
            while ((ptr = strchr(cl->buffer, '\n')) != NULL) {
                *ptr = '\0';
                char* msg_start = cl->buffer;
                /* 去掉末尾 \r */
                int msg_len = (int)strlen(msg_start);
                if (msg_len > 0 && msg_start[msg_len - 1] == '\r') {
                    msg_start[msg_len - 1] = '\0';
                }
                if (ts->msg_cb && msg_start[0]) {
                    ts->msg_cb(msg_start, cl->fd, ts->msg_cb_user_data);
                }

                /* 移动剩余数据到 buffer 头部 */
                memmove(cl->buffer, ptr + 1, strlen(ptr + 1) + 1);
            }
            i++;
        } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            /* 客户端断开或不可恢复错误 */
            close(cl->fd);
            /* 移除 */
            int j;
            for (j = i; j < ts->client_count - 1; j++) {
                ts->clients[j] = ts->clients[j + 1];
            }
            ts->client_count--;
            /* i 不变, 继续处理新的 clients[i] */
        } else {
            /* EAGAIN/EWOULDBLOCK 正常, 但计数持续错误 */
            if (n < 0) {
                cl->read_err_cnt++;
                if (cl->read_err_cnt > TCP_READ_ERR_MAX) {
                    fprintf(stderr, "[TcpServer] Closing fd=%d after %d consecutive read errors\n",
                            cl->fd, cl->read_err_cnt);
                    close(cl->fd);
                    int j;
                    for (j = i; j < ts->client_count - 1; j++) {
                        ts->clients[j] = ts->clients[j + 1];
                    }
                    ts->client_count--;
                    continue;  /* i 不变, 处理新 clients[i] */
                }
            }
            i++;
        }
    }
    pthread_mutex_unlock(&ts->client_mutex);
}
