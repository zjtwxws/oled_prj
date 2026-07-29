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
    /* 返回字符串会直接作为响应发回当前 WebSocket 连接 */
    using MessageCallback = std::function<std::string(const std::string& msg)>;

    WebServer();
    ~WebServer();

    /* 启动服务 (非阻塞) */
    int  start(int port = 80, const std::string& webRoot = "./web");
    void stop();
    bool isRunning() const;

    /* 设置收到 WebSocket 消息的回调
     * 回调返回的字符串会作为响应直接发回当前连接 */
    void onMessage(MessageCallback cb);

    /* 向所有连接的 WebSocket 客户端广播消息 */
    void broadcast(const std::string& msg);

    /* 主循环轮询 (保留以兼容上层, 现为空操作) */
    void pollEvents();

private:
    void serverThread();

    /* mongoose 事件处理回调 (C++ 静态成员可匹配 C 函数指针) */
    static void eventHandler(struct mg_connection* c, int ev, void* ev_data);

    int    m_port;
    std::string m_webRoot;
    std::atomic<bool> m_running;
    MessageCallback m_msgCb;

    /* mongoose 内部句柄 (void* 避免头文件依赖) */
    void* m_mgr;

    /* 用于跨线程广播的内部连接 id:
     * 主线程调用 mg_wakeup() 向该连接投递消息,
     * mongoose 线程在 MG_EV_WAKEUP 中执行真正的广播。 */
    unsigned long m_listenConnId;

    std::thread m_thread;
};

#endif /* WEB_SERVER_H */
