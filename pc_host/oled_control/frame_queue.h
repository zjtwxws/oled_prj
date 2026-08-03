#pragma once

/**
 * @file    frame_queue.h
 * @brief   线程安全环形队列模板 + 发送任务定义
 *
 * 用于解耦串口接收线程和 UI 处理线程。
 */

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

/* --- 线程安全环形队列模板 --- */
template<typename T, size_t Capacity>
class LockFreeQueue {
public:
    LockFreeQueue() : m_head(0), m_tail(0) {}

    bool Push(const T& item) {
        size_t head = m_head.load(std::memory_order_relaxed);
        size_t next = (head + 1) % Capacity;
        if (next == m_tail.load(std::memory_order_acquire))
            return false;  // 队列满
        m_buffer[head] = item;
        m_head.store(next, std::memory_order_release);
        return true;
    }

    bool Pop(T& item) {
        size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire))
            return false;  // 队列空
        item = m_buffer[tail];
        m_tail.store((tail + 1) % Capacity, std::memory_order_release);
        return true;
    }

    bool IsEmpty() const {
        return m_tail.load(std::memory_order_acquire) == m_head.load(std::memory_order_acquire);
    }

private:
    std::array<T, Capacity> m_buffer;
    std::atomic<size_t> m_head;
    std::atomic<size_t> m_tail;
};

/* --- 发送任务 --- */
struct TxTask {
    std::vector<uint8_t> frame;   // 完整帧数据 (SOF~EOF)
    uint8_t              cmd;     // 命令码
    uint8_t              seq;     // 序号
    uint64_t             sendTimeMs; // 发送时刻 (GetTickCount64)
    int                  retries; // 已重试次数

    TxTask() : cmd(0), seq(0), sendTimeMs(0), retries(0) {}
};
