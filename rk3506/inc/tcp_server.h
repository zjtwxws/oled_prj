/**
 * @file    tcp_server.h
 * @brief   TCP 服务器 (备选调试通道, 端口 9527) (纯 C)
 */

#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <stdint.h>
#include <pthread.h>

#define TCP_CLIENT_MAX 16

/* TCP 消息回调: msg 为接收到的消息, client_id 为客户端 fd */
typedef void (*tcp_msg_callback_t)(const char* msg, int client_id, void* user_data);

typedef struct {
    int    fd;
    char   buffer[4096];
    int    active;
} TcpClient;

typedef struct {
    int    listen_fd;
    int    port;
    int    running;

    tcp_msg_callback_t msg_cb;
    void*  msg_cb_user_data;

    TcpClient clients[TCP_CLIENT_MAX];
    int       client_count;
    pthread_mutex_t client_mutex;

    pthread_t thread;
} TcpServer;

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化/销毁 */
void tcp_init(TcpServer* ts);
void tcp_deinit(TcpServer* ts);

int  tcp_start(TcpServer* ts, int port);
void tcp_stop(TcpServer* ts);
int  tcp_is_running(const TcpServer* ts);

void tcp_on_message(TcpServer* ts, tcp_msg_callback_t cb, void* user_data);
void tcp_send(TcpServer* ts, int client_id, const char* msg);
void tcp_broadcast(TcpServer* ts, const char* msg);

/* 主循环轮询 */
void tcp_poll(TcpServer* ts);

#ifdef __cplusplus
}
#endif

#endif /* TCP_SERVER_H */
