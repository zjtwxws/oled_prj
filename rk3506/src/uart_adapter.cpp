/**
 * @file    uart_adapter.cpp
 * @brief   RK3506 端串口适配器实现
 */

#include "uart_adapter.h"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <cstring>
#include <iostream>

UartAdapter::UartAdapter() : m_fd(-1), m_running(false), m_seq(0) {}

UartAdapter::~UartAdapter() { close(); }

int UartAdapter::open(const std::string& device, int baud)
{
    m_device = device;
    m_fd = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_fd < 0) return -1;

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);
    tty.c_cflag = CS8 | CREAD | CLOCAL;
    tty.c_iflag = IGNPAR;
    tty.c_oflag = 0;
    tty.c_lflag = 0;
    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 1;
    tcflush(m_fd, TCIFLUSH);
    tcsetattr(m_fd, TCSANOW, &tty);

    m_running = true;
    m_rxThread = std::thread(&UartAdapter::readThread, this);
    return 0;
}

void UartAdapter::close()
{
    m_running = false;
    if (m_rxThread.joinable()) m_rxThread.join();
    if (m_fd >= 0) { ::close(m_fd); m_fd = -1; }
}

bool UartAdapter::isOpen() const { return m_fd >= 0 && m_running; }

void UartAdapter::onFrame(FrameCallback cb) { m_frameCb = cb; }

int UartAdapter::sendCommand(uint8_t cmd, const uint8_t* data, uint8_t len,
                              uint8_t expectedAckCmd, int timeoutMs)
{
    uint8_t seq = m_seq++;
    sendRaw(cmd, seq, data, len);

    PendingCmd pending;
    pending.cmd   = cmd;
    pending.seq   = seq;
    pending.retry = 0;
    pending.dataLen = (len > PROTO_MAX_DATA) ? PROTO_MAX_DATA : len;
    if (len > 0 && data) {
        memcpy(pending.data, data, pending.dataLen);
    }
    pending.sent  = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pending[seq] = pending;
    }

    /* 等待 ACK (简化: 在 poll 中处理, 此处返回 seq 供上层检查) */
    return seq;
}

void UartAdapter::sendRaw(uint8_t cmd, uint8_t seq, const uint8_t* data, uint8_t len)
{
    uint16_t frameLen = m_proto.build(cmd, seq, data, len);
    const uint8_t* buf = m_proto.txBuf();

    std::lock_guard<std::mutex> lock(m_txMutex);
    m_txQueue.push(std::vector<uint8_t>(buf, buf + frameLen));
}

void UartAdapter::poll()
{
    /* 发送队列 */
    {
        std::lock_guard<std::mutex> lock(m_txMutex);
        while (!m_txQueue.empty()) {
            auto& frame = m_txQueue.front();
            if (m_fd >= 0) {
                ::write(m_fd, frame.data(), frame.size());
            }
            m_txQueue.pop();
        }
    }

    /* ACK 超时重传 */
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        auto now = std::chrono::steady_clock::now();
        for (auto it = m_pending.begin(); it != m_pending.end(); ) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->second.sent).count();
            if (elapsed > UART_TIMEOUT_MS) {
                if (it->second.retry < UART_RETRY_MAX) {
                    it->second.retry++;
                    it->second.sent = now;
                    /* 重发 (使用保存的原始数据副本) */
                    uint16_t len = m_proto.build(it->second.cmd, it->second.seq,
                                                  it->second.data, it->second.dataLen);
                    if (m_fd >= 0) ::write(m_fd, m_proto.txBuf(), len);
                    ++it;
                } else {
                    std::cerr << "[UART] Command 0x" << std::hex << (int)it->second.cmd
                              << " timeout after " << UART_RETRY_MAX << " retries" << std::endl;
                    it = m_pending.erase(it);
                }
            } else {
                ++it;
            }
        }
    }
}

void UartAdapter::readThread()
{
    uint8_t buf[256];
    while (m_running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(m_fd, &fds);
        struct timeval tv = {0, 50000}; /* 50ms */
        int ret = select(m_fd + 1, &fds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(m_fd, &fds)) {
            ssize_t n = ::read(m_fd, buf, sizeof(buf));
            for (ssize_t i = 0; i < n; i++) {
                if (m_proto.feed(buf[i])) {
                    processRx(*m_proto.getFrame());
                }
            }
        }
    }
}

void UartAdapter::processRx(const ProtoFrame& frame)
{
    /* ACK/NAK 处理 */
    if (frame.cmd == CMD_ACK || frame.cmd == CMD_NAK) {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        auto it = m_pending.find(frame.seq);
        if (it != m_pending.end()) {
            m_pending.erase(it);
        }
    }

    /* 回调上层 */
    if (m_frameCb) {
        m_frameCb(frame);
    }
}
