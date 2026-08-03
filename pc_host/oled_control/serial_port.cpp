/**
 * @file    serial_port.cpp
 * @brief   Windows 串口通信驱动实现
 */

#define _WIN32_WINNT 0x0601
#include <windows.h>

#include "serial_port.h"
#include "serial_port.h"
#include <setupapi.h>
#include <devguid.h>

#pragma comment(lib, "setupapi.lib")

SerialPort::SerialPort()
    : m_hCom(INVALID_HANDLE_VALUE)
    , m_hRecvThread(nullptr)
    , m_running(false)
    , m_comNumber(0)
    , m_hWnd(nullptr)
{
}

SerialPort::~SerialPort()
{
    Close();
}

std::vector<SerialPort::ComInfo> SerialPort::Enumerate()
{
    std::vector<ComInfo> result;

    // 获取设备信息集 (所有串口)
    HDEVINFO hDevInfo = SetupDiGetClassDevs(
        &GUID_DEVCLASS_PORTS, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (hDevInfo == INVALID_HANDLE_VALUE)
        return result;

    // 枚举 COM1 ~ COM16
    for (int i = 1; i <= 16; ++i) {
        std::wstring comName = L"COM" + std::to_wstring(i);
        std::wstring fullPath = L"\\\\.\\" + comName;

        HANDLE hTest = CreateFileW(
            fullPath.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);

        if (hTest != INVALID_HANDLE_VALUE) {
            CloseHandle(hTest);

            // 尝试获取设备描述
            ComInfo info;
            info.number = i;
            info.name = comName;

            // 用 SetupDi 查友好名称
            SP_DEVINFO_DATA devInfoData = { sizeof(SP_DEVINFO_DATA) };
            for (DWORD idx = 0; SetupDiEnumDeviceInfo(hDevInfo, idx, &devInfoData); ++idx) {
                wchar_t buf[256] = {0};
                if (SetupDiGetDeviceRegistryPropertyW(
                        hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME,
                        nullptr, (PBYTE)buf, sizeof(buf), nullptr)) {
                    std::wstring friendly(buf);
                    if (friendly.find(comName) != std::wstring::npos) {
                        info.name = comName + L" - " + friendly;
                        break;
                    }
                }
            }
            result.push_back(info);
        }
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return result;
}

bool SerialPort::Open(int comNumber, DWORD baudRate)
{
    if (IsOpen()) Close();

    std::wstring path = L"\\\\.\\COM" + std::to_wstring(comNumber);

    m_hCom = CreateFileW(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,                  // 独占访问
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, // 异步 I/O
        nullptr);

    if (m_hCom == INVALID_HANDLE_VALUE) return false;

    // 设置串口参数
    DCB dcb = { sizeof(DCB) };
    if (!GetCommState(m_hCom, &dcb)) { Close(); return false; }

    dcb.BaudRate = baudRate;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(m_hCom, &dcb)) { Close(); return false; }

    // 设置超时
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout         = 50;   // 字节间隔 50ms
    timeouts.ReadTotalTimeoutMultiplier  = 0;
    timeouts.ReadTotalTimeoutConstant    = 100;  // 总超时 100ms
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant   = 100;

    if (!SetCommTimeouts(m_hCom, &timeouts)) { Close(); return false; }

    // 清空缓冲区
    PurgeComm(m_hCom, PURGE_RXCLEAR | PURGE_TXCLEAR);

    // 启动接收线程
    m_comNumber = comNumber;
    m_running = true;
    m_hRecvThread = CreateThread(nullptr, 0, RecvThreadProc, this, 0, nullptr);

    if (!m_hRecvThread) { Close(); return false; }

    return true;
}

void SerialPort::Close()
{
    m_running = false;

    if (m_hRecvThread) {
        WaitForSingleObject(m_hRecvThread, 2000);
        CloseHandle(m_hRecvThread);
        m_hRecvThread = nullptr;
    }

    if (m_hCom != INVALID_HANDLE_VALUE) {
        PurgeComm(m_hCom, PURGE_RXCLEAR | PURGE_TXCLEAR);
        CloseHandle(m_hCom);
        m_hCom = INVALID_HANDLE_VALUE;
    }
}

int SerialPort::Send(const uint8_t* data, size_t len)
{
    if (!IsOpen()) return -1;

    DWORD written = 0;
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    if (!WriteFile(m_hCom, data, (DWORD)len, &written, &ov)) {
        if (GetLastError() == ERROR_IO_PENDING) {
            GetOverlappedResult(m_hCom, &ov, &written, TRUE);
        }
    }

    CloseHandle(ov.hEvent);
    return (int)written;
}

DWORD WINAPI SerialPort::RecvThreadProc(LPVOID lpParam)
{
    auto* self = static_cast<SerialPort*>(lpParam);
    self->RecvLoop();
    return 0;
}

void SerialPort::RecvLoop()
{
    uint8_t buf[256];
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    while (m_running) {
        DWORD bytesRead = 0;
        ResetEvent(ov.hEvent);

        BOOL ok = ReadFile(m_hCom, buf, sizeof(buf), &bytesRead, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            DWORD waitResult = WaitForSingleObject(ov.hEvent, 100);
            if (waitResult == WAIT_OBJECT_0) {
                GetOverlappedResult(m_hCom, &ov, &bytesRead, FALSE);
                ok = TRUE;
            } else if (waitResult == WAIT_TIMEOUT) {
                // 超时, 继续循环检查 m_running
                CancelIo(m_hCom);
                continue;
            } else {
                // 等待失败
                break;
            }
        }

        if (ok && bytesRead > 0) {
            if (onDataReceived) {
                onDataReceived(buf, bytesRead);
            }
        } else if (!ok) {
            if (onError && m_running) {
                onError(L"串口读取错误，连接可能已断开");
            }
            break;
        }
    }

    CloseHandle(ov.hEvent);
}

