#pragma once

/**
 * @file    serial_port.h
 * @brief   Windows 串口通信驱动
 *
 * 使用 Win32 API (CreateFile/ReadFile/WriteFile) 实现。
 * 异步接收通过独立线程 + PostMessage 通知 UI 线程。
 */

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>

class SerialPort {
public:
    struct ComInfo {
        int         number;     // 串口号, 如 3 表示 COM3
        std::wstring name;      // 显示名称, 如 "COM3 - USB-SERIAL CH340"
    };

    SerialPort();
    ~SerialPort();

    // 禁止拷贝
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    // 枚举系统可用串口
    static std::vector<ComInfo> Enumerate();

    // 打开/关闭串口
    bool Open(int comNumber, DWORD baudRate = CBR_115200);
    void Close();
    bool IsOpen() const { return m_hCom != INVALID_HANDLE_VALUE; }

    // 发送数据 (同步, 阻塞调用)
    int  Send(const uint8_t* data, size_t len);

    // 回调函数 — 由主对话框设置
    // onDataReceived: 收到原始字节数据
    // onError:        串口错误 (断开、读取失败等)
    std::function<void(const uint8_t* data, size_t len)> onDataReceived;
    std::function<void(const std::wstring& msg)>         onError;

    // 获取当前串口号 (用于 UI 显示)
    int GetComNumber() const { return m_comNumber; }

private:
    HANDLE      m_hCom;
    HANDLE      m_hRecvThread;
    volatile bool m_running;
    int         m_comNumber;
    HWND        m_hWnd;         // 消息目标窗口

    static DWORD WINAPI RecvThreadProc(LPVOID lpParam);
    void RecvLoop();
};
