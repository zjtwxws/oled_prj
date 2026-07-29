/**
 * @file    web_server.c
 * @brief   Web 服务器实现 (基于 mongoose) (纯 C)
 *
 * 编译依赖: 下载 mongoose.c / mongoose.h 单文件库
 * https://github.com/cesanta/mongoose
 *
 * 编译命令示例:
 *   gcc -std=c11 -DMG_ENABLE_HTTP_WEBSOCKET=1 \
 *       -I../inc web_server.c mongoose.c -lpthread -o web_server
 */

#include "web_server.h"

/* mongoose 单头文件库 */
#include "mongoose.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- mongoose 事件处理 (C 静态函数) --- */

static void web_event_handler(struct mg_connection* c, int ev, void* ev_data)
{
    WebServer* self = (WebServer*)c->fn_data;
    if (!self) return;

    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message* hm = (struct mg_http_message*)ev_data;

        /* WebSocket 升级 */
        if (mg_match(hm->uri, mg_str("/ws"), NULL)) {
            mg_ws_upgrade(c, hm, NULL);
            return;
        }

        /* 静态文件服务 */
        struct mg_http_serve_opts opts;
        memset(&opts, 0, sizeof(opts));
        opts.root_dir = self->web_root;
        mg_http_serve_dir(c, hm, &opts);
        return;
    }

    if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message* wm = (struct mg_ws_message*)ev_data;
        char msg_buf[4096];
        int msg_len = (int)wm->data.len;
        if (msg_len > (int)(sizeof(msg_buf) - 1)) msg_len = (int)(sizeof(msg_buf) - 1);
        memcpy(msg_buf, wm->data.buf, msg_len);
        msg_buf[msg_len] = '\0';

        if (self->msg_cb) {
            const char* resp = self->msg_cb(msg_buf, self->msg_cb_user_data);
            if (resp && resp[0]) {
                mg_ws_send(c, resp, strlen(resp), WEBSOCKET_OP_TEXT);
            }
        }
        return;
    }

    /* 跨线程广播: 主线程通过 mg_wakeup 投递的消息在此处理 */
    if (ev == MG_EV_WAKEUP) {
        struct mg_str* data = (struct mg_str*)ev_data;
        if (data && data->buf && data->len > 0) {
            char payload[4096];
            int plen = (int)data->len;
            if (plen > (int)(sizeof(payload) - 1)) plen = (int)(sizeof(payload) - 1);
            memcpy(payload, data->buf, plen);
            payload[plen] = '\0';

            struct mg_mgr* mgr = (struct mg_mgr*)self->mgr;
            struct mg_connection* wc;
            for (wc = mgr->conns; wc != NULL; wc = wc->next) {
                if (wc->is_websocket) {
                    mg_ws_send(wc, payload, strlen(payload), WEBSOCKET_OP_TEXT);
                }
            }
        }
        return;
    }
}

/* --- 服务线程 --- */

static void* web_thread_func(void* arg)
{
    WebServer* self = (WebServer*)arg;
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    self->mgr = &mgr;

    /* 初始化跨线程唤醒管道 */
    mg_wakeup_init(&mgr);

    /* 监听端口 */
    char addr[32];
    snprintf(addr, sizeof(addr), "http://0.0.0.0:%d", self->port);

    struct mg_connection* listener = mg_http_listen(&mgr, addr, web_event_handler, self);
    if (listener) {
        self->listen_conn_id = listener->id;
    }

    printf("[WebServer] Listening on %s\n", addr);

    /* 主循环 */
    while (self->running) {
        mg_mgr_poll(&mgr, 50); /* 50ms timeout */
    }

    mg_mgr_free(&mgr);
    self->mgr = NULL;
    self->listen_conn_id = 0;
    return NULL;
}

/* --- 公共接口 --- */

void web_init(WebServer* ws)
{
    memset(ws, 0, sizeof(*ws));
    ws->port = 80;
}

void web_deinit(WebServer* ws)
{
    web_stop(ws);
}

int web_start(WebServer* ws, int port, const char* web_root)
{
    ws->port = (port > 0) ? port : 80;
    if (web_root) {
        strncpy(ws->web_root, web_root, sizeof(ws->web_root) - 1);
        ws->web_root[sizeof(ws->web_root) - 1] = '\0';
    } else {
        strcpy(ws->web_root, "./web");
    }
    ws->running = 1;
    pthread_create(&ws->thread, NULL, web_thread_func, ws);
    return 0;
}

void web_stop(WebServer* ws)
{
    ws->running = 0;
    if (ws->thread) {
        pthread_join(ws->thread, NULL);
        memset(&ws->thread, 0, sizeof(ws->thread));
    }
}

int web_is_running(const WebServer* ws)
{
    return ws->running;
}

void web_on_message(WebServer* ws, web_msg_callback_t cb, void* user_data)
{
    ws->msg_cb = cb;
    ws->msg_cb_user_data = user_data;
}

void web_broadcast(WebServer* ws, const char* msg)
{
    if (!ws->mgr || ws->listen_conn_id == 0) return;

    struct mg_mgr* mgr = (struct mg_mgr*)ws->mgr;
    mg_wakeup(mgr, ws->listen_conn_id, msg, strlen(msg));
}

void web_poll_events(WebServer* ws)
{
    (void)ws;
    /* 广播已改为通过 mg_wakeup 投递到 mongoose 线程内处理,
     * 此处无需操作, 保留接口兼容上层。 */
}
