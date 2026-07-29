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

static void mg_event_handler(struct mg_connection* c, int ev, void* ev_data)
{
    WebServer* self = (WebServer*)c->fn_data;

    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message* hm = (struct mg_http_message*)ev_data;

        /* WebSocket 升级 */
        if (mg_http_match_uri(hm, "/ws")) {
            mg_ws_upgrade(c, hm, NULL);
            return;
        }

        /* 静态文件服务 */
        struct mg_http_serve_opts opts = {};
        opts.root_dir = ".";
        mg_http_serve_dir(c, hm, &opts);
        return;
    }

    if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message* wm = (struct mg_ws_message*)ev_data;
        std::string msg((char*)wm->data.ptr, wm->data.len);

        if (self && self->m_msgCb) {
            self->m_msgCb(msg);
        }
    }
}

/* --- WebServer 实现 --- */

WebServer::WebServer() : m_port(80), m_running(false), m_mgr(nullptr) {}

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
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_broadcastQueue.push_back(msg);
}

void WebServer::poll()
{
    /* mongoose 使用自己的事件循环, poll 中处理广播队列 */
    if (!m_mgr) return;

    std::lock_guard<std::mutex> lock(m_queueMutex);
    for (auto& msg : m_broadcastQueue) {
        /* 遍历所有连接, 发送 WebSocket 消息 */
        struct mg_mgr* mgr = (struct mg_mgr*)m_mgr;
        for (struct mg_connection* c = mgr->conns; c != NULL; c = c->next) {
            if (c->is_websocket) {
                mg_ws_send(c, msg.c_str(), msg.length(), WEBSOCKET_OP_TEXT);
            }
        }
    }
    m_broadcastQueue.clear();
}

void WebServer::serverThread()
{
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    m_mgr = &mgr;

    /* 监听端口 */
    char addr[32];
    snprintf(addr, sizeof(addr), "http://0.0.0.0:%d", m_port);

    /* 传入 this 指针作为 fn_data, 替代 WebServerCtx 结构体 */
    mg_http_listen(&mgr, addr, mg_event_handler, this);

    std::cout << "[WebServer] Listening on " << addr << std::endl;

    /* 主循环 */
    while (m_running) {
        mg_mgr_poll(&mgr, 50); /* 50ms timeout */
    }

    mg_mgr_free(&mgr);
    m_mgr = nullptr;
}
