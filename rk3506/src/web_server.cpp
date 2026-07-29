/**
 * @file    web_server.cpp
 * @brief   Web 服务器实现 (基于 mongoose)
 *
 * 编译依赖: 下载 mongoose.c / mongoose.h 单文件库
 * https://github.com/cesanta/mongoose
 *
 * 编译命令示例:
 *   g++ -std=c++11 -DMG_ENABLE_HTTP_WEBSOCKET=1 \
 *       -I../inc web_server.cpp mongoose.c -lpthread -o web_server
 */

#include "web_server.h"

/* mongoose 单头文件库 */
#include "mongoose.h"

#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>

/* --- mongoose 事件处理 --- */

void WebServer::eventHandler(struct mg_connection* c, int ev, void* ev_data)
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
        struct mg_http_serve_opts opts = {};
        opts.root_dir = self->m_webRoot.c_str();
        mg_http_serve_dir(c, hm, &opts);
        return;
    }

    if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message* wm = (struct mg_ws_message*)ev_data;
        std::string msg((char*)wm->data.buf, wm->data.len);

        if (self->m_msgCb) {
            std::string resp = self->m_msgCb(msg);
            if (!resp.empty()) {
                mg_ws_send(c, resp.c_str(), resp.length(), WEBSOCKET_OP_TEXT);
            }
        }
        return;
    }

    /* 跨线程广播: 主线程通过 mg_wakeup 投递的消息在此处理 */
    if (ev == MG_EV_WAKEUP) {
        struct mg_str* data = (struct mg_str*)ev_data;
        if (data && data->buf && data->len > 0) {
            std::string payload(data->buf, data->len);
            struct mg_mgr* mgr = (struct mg_mgr*)self->m_mgr;
            for (struct mg_connection* wc = mgr->conns; wc != NULL; wc = wc->next) {
                if (wc->is_websocket) {
                    mg_ws_send(wc, payload.c_str(), payload.length(), WEBSOCKET_OP_TEXT);
                }
            }
        }
        return;
    }
}

/* --- WebServer 实现 --- */

WebServer::WebServer() : m_port(80), m_running(false), m_mgr(nullptr), m_listenConnId(0) {}

WebServer::~WebServer() { stop(); }

int WebServer::start(int port, const std::string& webRoot)
{
    m_port    = port;
    m_webRoot = webRoot;
    m_running = true;
    m_thread  = std::thread(&WebServer::serverThread, this);
    return 0;
}

void WebServer::stop()
{
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
}

bool WebServer::isRunning() const { return m_running; }

void WebServer::onMessage(MessageCallback cb) { m_msgCb = cb; }

void WebServer::broadcast(const std::string& msg)
{
    if (!m_mgr || m_listenConnId == 0) return;

    struct mg_mgr* mgr = (struct mg_mgr*)m_mgr;
    /* mg_wakeup 是 mongoose 提供的跨线程唤醒/投递 API,
     * 向 listen 连接投递 MG_EV_WAKEUP 事件, 在事件循环线程内执行广播。 */
    mg_wakeup(mgr, m_listenConnId, msg.c_str(), msg.length());
}

void WebServer::pollEvents()
{
    /* 广播已改为通过 mg_wakeup 投递到 mongoose 线程内处理,
     * 此处无需操作, 保留接口兼容上层。 */
}

void WebServer::serverThread()
{
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    m_mgr = &mgr;

    /* 初始化跨线程唤醒管道 */
    mg_wakeup_init(&mgr);

    /* 监听端口 */
    char addr[32];
    snprintf(addr, sizeof(addr), "http://0.0.0.0:%d", m_port);

    /* 传入 this 指针作为 fn_data */
    struct mg_connection* listener = mg_http_listen(&mgr, addr, &WebServer::eventHandler, this);
    if (listener) {
        m_listenConnId = listener->id;
    }

    std::cout << "[WebServer] Listening on " << addr << std::endl;

    /* 主循环 */
    while (m_running) {
        mg_mgr_poll(&mgr, 50); /* 50ms timeout */
    }

    mg_mgr_free(&mgr);
    m_mgr = nullptr;
    m_listenConnId = 0;
}
