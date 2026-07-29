/**
 * @file    uart_adapter.h
 * @brief   RK3506 端串口适配器 — Linux 串口封装 + 协议收发 + ACK/重传
 */

#ifndef UART_ADAPTER_H
#define UART_ADAPTER_H

#include "protocol.h"
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include <map>
#include <queue>
#include <mutex>

#define UART_RETRY_MAX      3
#define UART_TIMEOUT_MS     500

class UartAdapter {
public:
    using FrameCallback = std::function<void(const ProtoFrame&)>;

    UartAdapter();
    ~UartAdapter();

    int  open(const std::string& device, int baud = 115200);
    void close();
    bool isOpen() const;

    /* 设置收到 STM 帧的回调 */
    void onFrame(FrameCallback cb);

    /* 发送指令 (带 ACK 等待) */
    int  sendCommand(uint8_t cmd, const uint8_t* data, uint8_t len,
                     uint8_t expectedAckCmd, int timeoutMs = UART_TIMEOUT_MS);

    /* 发送原始帧 (不等待 ACK) */
    void sendRaw(uint8_t cmd, uint8_t seq, const uint8_t* data, uint8_t len);

    /* 事件轮询 (需在主循环中调用) */
    void poll();

private:
    void readThread();
    void processRx(const ProtoFrame& frame);

    int         m_fd;
    std::string m_device;
    std::atomic<bool> m_running;
    std::atomic<uint8_t> m_seq;

    FrameCallback m_frameCb;
    Protocol    m_proto;

    /* ACK 等待 */
    struct PendingCmd {
        uint8_t cmd;
        uint8_t seq;
        int     retry;
        uint8_t data[PROTO_MAX_DATA];  /* 原始数据副本(用于重传) */
        uint8_t dataLen;
        std::chrono::steady_clock::time_point sent;
    };
    std::map<uint8_t, PendingCmd> m_pending;
    std::mutex m_pendingMutex;          /* 保护 m_pending 的互斥锁 */

    /* 待发送队列 */
    std::queue<std::vector<uint8_t>> m_txQueue;
    std::mutex m_txMutex;

    std::thread m_rxThread;
};

#endif /* UART_ADAPTER_H */
