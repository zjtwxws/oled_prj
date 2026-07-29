/**
 * @file    web_server.h
 * @brief   RK3506 Web 服务器 — HTTP 静态资源 + WebSocket 双向通信 (纯 C)
 *
 * 使用 mongoose 轻量级网络库 (单头文件, 无需外部依赖).
 * 编译: gcc -DMG_ENABLE_HTTP_WEBSOCKET=1 ...
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <stdint.h>
#include <pthread.h>

/* WebSocket 消息回调: 接收消息字符串, 返回响应字符串 (静态缓冲区) */
typedef const char* (*web_msg_callback_t)(const char* msg, void* user_data);

typedef struct {
    int    port;
    char   web_root[256];
    int    running;

    web_msg_callback_t msg_cb;
    void*  msg_cb_user_data;

    /* mongoose 内部句柄 (void* 避免头文件依赖) */
    void*  mgr;

    /* 用于跨线程广播的内部连接 id */
    unsigned long listen_conn_id;

    pthread_t thread;
} WebServer;

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化/销毁 */
void web_init(WebServer* ws);
void web_deinit(WebServer* ws);

/* 启动服务 (非阻塞), port=0 使用默认 80 */
int  web_start(WebServer* ws, int port, const char* web_root);
void web_stop(WebServer* ws);
int  web_is_running(const WebServer* ws);

/* 设置收到 WebSocket 消息的回调 */
void web_on_message(WebServer* ws, web_msg_callback_t cb, void* user_data);

/* 向所有连接的 WebSocket 客户端广播消息 */
void web_broadcast(WebServer* ws, const char* msg);

/* 主循环轮询 (保留以兼容上层, 现为空操作) */
void web_poll_events(WebServer* ws);

#ifdef __cplusplus
}
#endif

#endif /* WEB_SERVER_H */
