/**
 * @file    web_server.h
 * @brief   RK3506 Web 服务器 — HTTP 静态资源 + WebSocket 双向通信
 *
 * 使用 mongoose 轻量级网络库 (单头文件, 无需外部依赖).
 * 编译: g++ -DMG_ENABLE_HTTP_WEBSOCKET=1 ...
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>

class WebServer {
public:
    using MessageCallback = std::function<void(const std::string& msg)>;

    WebServer();
    ~WebServer();

    /* 启动服务 (非阻塞) */
    int  start(int port = 80, const std::string& webRoot = "./web");
    void stop();
    bool isRunning() const;

    /* 设置收到 WebSocket 消息的回调 */
    void onMessage(MessageCallback cb);

    /* 向所有连接的 WebSocket 客户端广播消息 */
    void broadcast(const std::string& msg);

    /* 主循环 poll (需周期性调用) */
    void poll();

private:
    void serverThread();

    int    m_port;
    std::string m_webRoot;
    std::atomic<bool> m_running;
    MessageCallback m_msgCb;

    /* mongoose 内部句柄 (void* 避免头文件依赖) */
    void* m_mgr;

    /* 待广播消息队列 */
    std::vector<std::string> m_broadcastQueue;
    std::mutex m_queueMutex;

    std::thread m_thread;
};

#endif /* WEB_SERVER_H */
