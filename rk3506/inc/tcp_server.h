/**
 * @file    tcp_server.h
 * @brief   TCP 服务器 (备选调试通道, 端口 9527)
 */

#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>

class TcpServer {
public:
    using MessageCallback = std::function<void(const std::string& msg, int clientId)>;

    TcpServer();
    ~TcpServer();

    int  start(int port = 9527);
    void stop();
    bool isRunning() const;

    void onMessage(MessageCallback cb);
    void send(int clientId, const std::string& msg);
    void broadcast(const std::string& msg);

    void poll();

private:
    void serverThread();
    void acceptClients();
    void handleClient(int fd);

    int    m_listenFd;
    int    m_port;
    std::atomic<bool> m_running;
    MessageCallback m_msgCb;

    struct Client {
        int fd;
        std::string buffer;
    };
    std::vector<Client> m_clients;
    std::mutex m_clientMutex;

    std::thread m_thread;
};

#endif /* TCP_SERVER_H */
