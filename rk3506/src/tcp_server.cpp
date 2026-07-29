/**
 * @file    tcp_server.cpp
 * @brief   TCP 服务器实现
 */

#include "tcp_server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <algorithm>

TcpServer::TcpServer() : m_listenFd(-1), m_port(9527), m_running(false) {}
TcpServer::~TcpServer() { stop(); }

int TcpServer::start(int port)
{
    m_port = port;
    m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenFd < 0) return -1;

    int opt = 1;
    setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    fcntl(m_listenFd, F_SETFL, O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(m_port);

    if (bind(m_listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(m_listenFd);
        return -2;
    }
    listen(m_listenFd, 5);

    m_running = true;
    m_thread = std::thread(&TcpServer::serverThread, this);
    std::cout << "[TcpServer] Listening on port " << m_port << std::endl;
    return 0;
}

void TcpServer::stop()
{
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
    if (m_listenFd >= 0) { close(m_listenFd); m_listenFd = -1; }
}

bool TcpServer::isRunning() const { return m_running; }
void TcpServer::onMessage(MessageCallback cb) { m_msgCb = cb; }

void TcpServer::send(int clientId, const std::string& msg)
{
    std::lock_guard<std::mutex> lock(m_clientMutex);
    for (auto& c : m_clients) {
        if (c.fd == clientId) {
            ::write(c.fd, msg.c_str(), msg.length());
            break;
        }
    }
}

void TcpServer::broadcast(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(m_clientMutex);
    for (auto& c : m_clients) {
        ::write(c.fd, msg.c_str(), msg.length());
    }
}

void TcpServer::poll()
{
    /* 接受新连接 */
    if (m_listenFd >= 0) {
        struct sockaddr_in cli;
        socklen_t len = sizeof(cli);
        int fd = accept(m_listenFd, (struct sockaddr*)&cli, &len);
        if (fd >= 0) {
            fcntl(fd, F_SETFL, O_NONBLOCK);
            std::lock_guard<std::mutex> lock(m_clientMutex);
            m_clients.push_back({fd, ""});
            std::cout << "[TcpServer] Client connected: fd=" << fd << std::endl;
        }
    }

    /* 读取客户端数据 */
    char buf[1024];
    std::lock_guard<std::mutex> lock(m_clientMutex);
    for (auto it = m_clients.begin(); it != m_clients.end(); ) {
        ssize_t n = ::read(it->fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            it->buffer += buf;
            /* 按 '\n' 分割消息 */
            size_t pos;
            while ((pos = it->buffer.find('\n')) != std::string::npos) {
                std::string msg = it->buffer.substr(0, pos);
                it->buffer.erase(0, pos + 1);
                if (!msg.empty() && msg.back() == '\r') msg.pop_back();
                if (m_msgCb && !msg.empty()) m_msgCb(msg, it->fd);
            }
            ++it;
        } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            close(it->fd);
            it = m_clients.erase(it);
        } else {
            ++it;
        }
    }
}

void TcpServer::serverThread()
{
    while (m_running) {
        poll();
        usleep(10000); /* 10ms */
    }
}
