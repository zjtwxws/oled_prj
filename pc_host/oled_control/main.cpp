/**
 * @file    main.cpp
 * @brief   OLED 控制中心 — Win32 对话框应用程序入口
 */

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <cstdio>
#include <string>
#include <vector>
#include <deque>
#include <ctime>
#include "resource.h"
#include "protocol.h"
#include "serial_port.h"
#include "frame_queue.h"
#include "oled_preview.h"

#pragma comment(lib, "comctl32.lib")

/* ============================================================
 * 全局状态
 * ============================================================ */
static HINSTANCE   g_hInst;
static HWND        g_hDlg;
static HWND        g_hCboPort, g_hBtnOpen, g_hBtnClose, g_hStcConn;
static HWND        g_hRadioLed[3];
static HWND        g_hRadioMode[7];
static HWND        g_hEditText, g_hBtnSendText;
static HWND        g_hEditBootText, g_hBtnSaveBoot;
static HWND        g_hCboWeather, g_hEditTemp, g_hEditHumidity, g_hCboWind, g_hBtnSendWeather;
static HWND        g_hBtnSyncTime;
static HWND        g_hStcLedStatus, g_hStcModeStatus, g_hStcLatency;
static HWND        g_hGrpDeviceStatus, g_hGrpKeyLog;
static HWND        g_hLstKeyLog;
static HWND        g_hOledPreview;

static SerialPort  g_serial;
static ProtocolParser g_parser;
static std::deque<TxTask> g_pendingAcks;
static uint8_t     g_seq = 0;
static bool        g_connected = false;

/* OLED preview instance */
static OledPreview g_preview;

static const wchar_t* g_modeNames[] = {
    L"静态", L"左滚", L"右滚", L"上滚", L"下滚", L"翻页", L"淡入淡出"
};

/* ============================================================
 * 辅助函数
 * ============================================================ */
static void AppendLog(const wchar_t* fmt, ...) {
    wchar_t buf[256];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t entry[320];
    _snwprintf_s(entry, _TRUNCATE, L"[%02d:%02d:%02d] %s", st.wHour, st.wMinute, st.wSecond, buf);

    ListBox_AddString(g_hLstKeyLog, entry);
    int cnt = ListBox_GetCount(g_hLstKeyLog);
    if (cnt > 100) ListBox_DeleteString(g_hLstKeyLog, 0);
    ListBox_SetTopIndex(g_hLstKeyLog, cnt - 1);
}

static void SendCommand(uint8_t cmd, const uint8_t* data, uint8_t len) {
    if (!g_serial.IsOpen()) return;
    uint8_t seq = g_seq++;
    auto frame = ProtocolParser::BuildFrame(cmd, seq, data, len);
    int sent = g_serial.Send(frame.data(), frame.size());
    if (sent > 0) {
        TxTask task;
        task.frame = std::move(frame);
        task.cmd = cmd;
        task.seq = seq;
        task.sendTimeMs = GetTickCount64();
        task.retries = 0;
        g_pendingAcks.push_back(task);
    }
}

static void CheckRetransmit() {
    uint64_t now = GetTickCount64();
    while (!g_pendingAcks.empty()) {
        TxTask& t = g_pendingAcks.front();
        if (now - t.sendTimeMs < 500) break;
        if (t.retries >= 3) {
            AppendLog(L"通信超时：命令 0x%02X 未收到ACK", t.cmd);
            g_pendingAcks.pop_front();
            continue;
        }
        t.retries++;
        t.sendTimeMs = now;
        g_serial.Send(t.frame.data(), t.frame.size());
    }
}

static void ProcessFrame(const ProtoFrame& frame) {
    switch (frame.cmd) {
        case CMD_ACK: {
        bool found = false;
        for (auto it = g_pendingAcks.begin(); it != g_pendingAcks.end(); ++it) {
            if (it->seq == frame.seq) {
                g_pendingAcks.erase(it);
                found = true;
                break;
            }
        }
        if (!found) {
            AppendLog(L"ACK seq=%u 无匹配待确认命令", frame.seq);
        }
        break;
    }
    case CMD_LED_STATUS:
        if (frame.len >= 1) {
            const wchar_t* states[] = {L"LED: 关", L"LED: 开", L"LED: 闪烁"};
            SetWindowText(g_hStcLedStatus, states[frame.data[0] % 3]);
            g_preview.SetLedState(frame.data[0]);
        }
        break;
    case CMD_MODE_STATUS:
        if (frame.len >= 1) {
            SetWindowText(g_hStcModeStatus, g_modeNames[frame.data[0] % 7]);
        }
        break;
    case CMD_KEY_EVENT:
        if (frame.len >= 2)
            AppendLog(L"KEY%d %s", frame.data[0],
                frame.data[1] == 0 ? L"短按" : frame.data[1] == 1 ? L"长按" : L"释放");
        break;
    }
}

/* --- 串口数据到达回调 --- */
static void OnSerialData(const uint8_t* data, size_t len) {
    // Hex dump received raw bytes (first 40)
    wchar_t hex[256], *p = hex;
    for (size_t i = 0; i < len && i < 40; i++) {
        p += _snwprintf_s(p, 256 - (p - hex), _TRUNCATE, L"%02X ", data[i]);
    }
    if (len > 40) { _snwprintf_s(p, 256 - (p - hex), _TRUNCATE, L"...(%zu)", len); }
    AppendLog(L"RX: %s", hex);

    for (size_t i = 0; i < len; i++) {
        if (g_parser.FeedByte(data[i])) {
            const ProtoFrame& f = g_parser.GetFrame();
            AppendLog(L"  -> cmd=0x%02X seq=%u len=%u", f.cmd, f.seq, f.len);
            ProcessFrame(f);
        }
    }
}

static void OnSerialError(const std::wstring& msg) {
    g_serial.Close();
    g_connected = false;
    SetWindowText(g_hStcConn, L"○ 未连接");
    EnableWindow(g_hBtnOpen, TRUE);
    EnableWindow(g_hBtnClose, FALSE);
    AppendLog(L"串口错误：%s", msg.c_str());
}

/* --- 串口枚举 --- */
static void PopulateComPorts() {
    ComboBox_ResetContent(g_hCboPort);
    auto ports = SerialPort::Enumerate();
    for (auto& p : ports)
        ComboBox_AddString(g_hCboPort, p.name.c_str());
    if (ComboBox_GetCount(g_hCboPort) > 0)
        ComboBox_SetCurSel(g_hCboPort, 0);
}

static void SyncTime() {
    time_t now = time(nullptr); struct tm t;
    localtime_s(&t, &now);
    uint8_t d[7] = {
        (uint8_t)(t.tm_year + 1900 - 2000), (uint8_t)(t.tm_mon + 1),
        (uint8_t)t.tm_mday, (uint8_t)t.tm_hour, (uint8_t)t.tm_min,
        (uint8_t)t.tm_sec, (uint8_t)t.tm_wday
    };
    SendCommand(CMD_TIME_SYNC, d, 7);
    g_preview.SetTime(t.tm_hour, t.tm_min, t.tm_sec, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_wday);
}

/* --- 对话框单位换算 --- */
static void GetDlgUnitScale(HWND hDlg, int& unitX, int& unitY) {
    RECT rc = { 0, 0, 100, 100 };
    MapDialogRect(hDlg, &rc);
    unitX = rc.right;
    unitY = rc.bottom;
}

static int DluToPx(int dlu, int unit) {
    return MulDiv(dlu, unit, 100);
}

static void LayoutMainDialog(HWND hDlg) {
    if (!g_hOledPreview || !g_hGrpDeviceStatus || !g_hGrpKeyLog || !g_hLstKeyLog)
        return;

    RECT rcClient;
    GetClientRect(hDlg, &rcClient);
    int unitX = 100, unitY = 100;
    GetDlgUnitScale(hDlg, unitX, unitY);

    int rightX = DluToPx(220, unitX);
    int margin = DluToPx(10, unitX);
    int rightW = rcClient.right - rightX - margin;
    if (rightW < DluToPx(200, unitX)) rightW = DluToPx(200, unitX);

    SetWindowPos(g_hOledPreview, nullptr, rightX, DluToPx(10, unitY), rightW, DluToPx(270, unitY), SWP_NOZORDER);

    int statusTop = DluToPx(290, unitY);
    SetWindowPos(g_hGrpDeviceStatus, nullptr, rightX, statusTop, rightW, DluToPx(55, unitY), SWP_NOZORDER);

    int innerX = rightX + DluToPx(10, unitX);
    int innerW = rightW - 2 * DluToPx(10, unitX);
    int labelW = DluToPx(90, unitX);
    SetWindowPos(g_hStcConn, nullptr, innerX, statusTop + DluToPx(12, unitY), innerW, DluToPx(14, unitY), SWP_NOZORDER);
    SetWindowPos(g_hStcLedStatus, nullptr, innerX, statusTop + DluToPx(28, unitY), labelW, DluToPx(14, unitY), SWP_NOZORDER);
    SetWindowPos(g_hStcModeStatus, nullptr, innerX + DluToPx(100, unitX), statusTop + DluToPx(28, unitY), labelW, DluToPx(14, unitY), SWP_NOZORDER);
    SetWindowPos(g_hStcLatency, nullptr, innerX + DluToPx(200, unitX), statusTop + DluToPx(28, unitY), labelW, DluToPx(14, unitY), SWP_NOZORDER);

    int logTop = DluToPx(355, unitY);
    int logBottom = rcClient.bottom - margin;
    if (logBottom < logTop + DluToPx(80, unitY)) logBottom = logTop + DluToPx(80, unitY);
    SetWindowPos(g_hGrpKeyLog, nullptr, rightX, logTop, rightW, logBottom - logTop, SWP_NOZORDER);

    int listTop = DluToPx(373, unitY);
    SetWindowPos(g_hLstKeyLog, nullptr, innerX, listTop, innerW, logBottom - DluToPx(8, unitY) - listTop, SWP_NOZORDER);

    InvalidateRect(g_hOledPreview, nullptr, TRUE);
}

static void UpdatePreviewText() {
    wchar_t buf[512] = { 0 };
    GetWindowText(g_hEditText, buf, 512);
    int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    if (len > 201) len = 201;
    std::vector<char> utf8(len);
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8.data(), len, nullptr, nullptr);
    utf8[len - 1] = '\0';
    g_preview.SetText(utf8.data());
}

static void UpdatePreviewWeather() {
    wchar_t tmp[16];
    GetWindowText(g_hEditTemp, tmp, 16);
    int temp = _wtoi(tmp);
    GetWindowText(g_hEditHumidity, tmp, 16);
    int humidity = _wtoi(tmp);
    int weather = ComboBox_GetCurSel(g_hCboWeather);
    int wind = ComboBox_GetCurSel(g_hCboWind);
    g_preview.SetWeather(weather < 0 ? 0 : weather, temp, humidity, wind < 0 ? 0 : wind);
}

/* ============================================================
 * 对话框消息处理
 * ============================================================ */
INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        g_hDlg = hDlg;

        // Bind control handles
        g_hCboPort      = GetDlgItem(hDlg, IDC_COMBO_PORT);
        g_hBtnOpen      = GetDlgItem(hDlg, IDC_BTN_OPEN);
        g_hBtnClose     = GetDlgItem(hDlg, IDC_BTN_CLOSE);
        g_hStcConn      = GetDlgItem(hDlg, IDC_STC_CONN_STATUS);
        for (int i = 0; i < 3; i++) g_hRadioLed[i] = GetDlgItem(hDlg, IDC_RADIO_LED_OFF + i);
        for (int i = 0; i < 7; i++) g_hRadioMode[i] = GetDlgItem(hDlg, IDC_RADIO_MODE_BASE + i);
        g_hEditText     = GetDlgItem(hDlg, IDC_EDIT_TEXT);
        g_hBtnSendText  = GetDlgItem(hDlg, IDC_BTN_SEND_TEXT);
        g_hEditBootText = GetDlgItem(hDlg, IDC_EDIT_BOOT_TEXT);
        g_hBtnSaveBoot  = GetDlgItem(hDlg, IDC_BTN_SAVE_BOOT);
        g_hCboWeather   = GetDlgItem(hDlg, IDC_CBO_WEATHER_TYPE);
        g_hEditTemp     = GetDlgItem(hDlg, IDC_EDIT_TEMP);
        g_hEditHumidity = GetDlgItem(hDlg, IDC_EDIT_HUMIDITY);
        g_hCboWind      = GetDlgItem(hDlg, IDC_CBO_WIND);
        g_hBtnSendWeather = GetDlgItem(hDlg, IDC_BTN_SEND_WEATHER);
        g_hBtnSyncTime  = GetDlgItem(hDlg, IDC_BTN_SYNC_TIME);
        g_hStcLedStatus = GetDlgItem(hDlg, IDC_STC_LED_STATUS);
        g_hStcModeStatus = GetDlgItem(hDlg, IDC_STC_MODE_STATUS);
        g_hStcLatency   = GetDlgItem(hDlg, IDC_STC_LATENCY);
        g_hLstKeyLog    = GetDlgItem(hDlg, IDC_LST_KEY_LOG);
        g_hGrpKeyLog    = GetDlgItem(hDlg, IDC_GRP_KEY_LOG);
        g_hGrpDeviceStatus = GetDlgItem(hDlg, IDC_GRP_DEVICE_STATUS);
        g_hOledPreview  = GetDlgItem(hDlg, IDC_OLED_PREVIEW);

        // Init controls
        Button_SetCheck(g_hRadioLed[0], BST_CHECKED);
        Button_SetCheck(g_hRadioMode[0], BST_CHECKED);
        SetWindowText(g_hEditBootText, L"欢迎进入系统");
        SetWindowText(g_hEditTemp, L"25");
        SetWindowText(g_hEditHumidity, L"65");

        // Weather combo
        const wchar_t* weather[] = {L"晴", L"多云", L"阴", L"小雨", L"大雨", L"雷雨", L"雪"};
        for (int i = 0; i < 7; i++) ComboBox_AddString(g_hCboWeather, weather[i]);
        ComboBox_SetCurSel(g_hCboWeather, 0);

        // Wind combo
        const wchar_t* wind[] = {L"北", L"东北", L"东", L"东南", L"南", L"西南", L"西", L"西北"};
        for (int i = 0; i < 8; i++) ComboBox_AddString(g_hCboWind, wind[i]);
        ComboBox_SetCurSel(g_hCboWind, 0);

        PopulateComPorts();
        SetWindowText(g_hStcConn, L"○ 未连接");
        EnableWindow(g_hBtnClose, FALSE);

        // Init OLED preview
        g_preview.Attach(g_hOledPreview);
        LayoutMainDialog(hDlg);

        // Setup serial callbacks
        g_serial.onDataReceived = OnSerialData;
        g_serial.onError = OnSerialError;

        // Timer for animation + retransmit
        SetTimer(hDlg, 1, 50, nullptr);
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BTN_OPEN: {
            int sel = ComboBox_GetCurSel(g_hCboPort);
            auto ports = SerialPort::Enumerate();
            if (sel >= 0 && sel < (int)ports.size()) {
                if (g_serial.Open(ports[sel].number)) {
                    g_connected = true;
                    SetWindowText(g_hStcConn, L"● 已连接");
                    EnableWindow(g_hBtnOpen, FALSE);
                    EnableWindow(g_hBtnClose, TRUE);
                    g_seq = 0;
                    SyncTime();
                    AppendLog(L"串口已连接");
                } else {
                    MessageBox(hDlg, L"无法打开串口", L"错误", MB_ICONERROR);
                }
            }
            break;
        }
        case IDC_BTN_CLOSE:
            g_serial.Close();
            g_connected = false;
            while (!g_pendingAcks.empty()) g_pendingAcks.pop_front();
            SetWindowText(g_hStcConn, L"○ 未连接");
            EnableWindow(g_hBtnOpen, TRUE);
            EnableWindow(g_hBtnClose, FALSE);
            AppendLog(L"串口已断开");
            break;

        case IDC_RADIO_LED_OFF: case IDC_RADIO_LED_ON: case IDC_RADIO_LED_BLINK: {
            uint8_t state = 0;
            if (Button_GetCheck(g_hRadioLed[1]) == BST_CHECKED) state = 1;
            else if (Button_GetCheck(g_hRadioLed[2]) == BST_CHECKED) state = 2;
            SendCommand(CMD_LED_CTRL, &state, 1);
            g_preview.SetLedState(state);
            break;
        }
        case IDC_BTN_SEND_TEXT: {
            wchar_t buf[512] = {0};
            GetWindowText(g_hEditText, buf, 512);
            int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
            if (len > 201) len = 201;
            std::vector<char> utf8(len);
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8.data(), len, nullptr, nullptr);
            utf8[len-1] = '\0';
            if (utf8[0]) {
                int textLen = (int)strlen(utf8.data());
                SendCommand(CMD_TEXT_CONTENT, (const uint8_t*)utf8.data(), (uint8_t)textLen);
                g_preview.SetText(utf8.data());
            }
            break;
        }
        case IDC_BTN_SAVE_BOOT: {
            wchar_t buf[256] = {0};
            GetWindowText(g_hEditBootText, buf, 256);
            int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
            if (len > 129) len = 129;
            std::vector<char> utf8(len);
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8.data(), len, nullptr, nullptr);
            utf8[len-1] = '\0';
            int textLen = (int)strlen(utf8.data());
            if (textLen > 0)
                SendCommand(CMD_BOOT_TEXT, (const uint8_t*)utf8.data(), (uint8_t)textLen);
            break;
        }
        case IDC_BTN_SEND_WEATHER: {
            uint8_t d[4] = {0};
            d[0] = (uint8_t)ComboBox_GetCurSel(g_hCboWeather);
            wchar_t tmp[16];
            GetWindowText(g_hEditTemp, tmp, 16); d[1] = (uint8_t)_wtoi(tmp);
            GetWindowText(g_hEditHumidity, tmp, 16); d[2] = (uint8_t)_wtoi(tmp);
            d[3] = (uint8_t)ComboBox_GetCurSel(g_hCboWind);
            SendCommand(CMD_WEATHER_DATA, d, 4);
            g_preview.SetWeather(d[0], d[1], d[2], d[3]);
            break;
        }
        case IDC_EDIT_TEXT:
            if (HIWORD(wParam) == EN_CHANGE) UpdatePreviewText();
            break;
        case IDC_EDIT_TEMP:
        case IDC_EDIT_HUMIDITY:
            if (HIWORD(wParam) == EN_CHANGE) UpdatePreviewWeather();
            break;
        case IDC_CBO_WEATHER_TYPE:
        case IDC_CBO_WIND:
            if (HIWORD(wParam) == CBN_SELCHANGE) UpdatePreviewWeather();
            break;
        case IDC_BTN_SYNC_TIME:
            SyncTime();
            break;

        // Mode radios
        case IDC_RADIO_MODE_BASE: case IDC_RADIO_MODE_BASE+1: case IDC_RADIO_MODE_BASE+2:
        case IDC_RADIO_MODE_BASE+3: case IDC_RADIO_MODE_BASE+4:
        case IDC_RADIO_MODE_BASE+5: case IDC_RADIO_MODE_BASE+6:
            if (HIWORD(wParam) == BN_CLICKED) {
                for (int i = 0; i < 7; i++) {
                    if (Button_GetCheck(g_hRadioMode[i]) == BST_CHECKED) {
                        uint8_t m = (uint8_t)i;
                        SendCommand(CMD_DISPLAY_MODE, &m, 1);
                        g_preview.SetMode(i);
                        break;
                    }
                }
            }
            break;
        }
        break;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        RECT rcMin = { 0, 0, 620, 520 };
        MapDialogRect(hDlg, &rcMin);
        mmi->ptMinTrackSize.x = rcMin.right;
        mmi->ptMinTrackSize.y = rcMin.bottom;
        return 0;
    }

    case WM_SIZE:
        if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)
            LayoutMainDialog(hDlg);
        break;


    case WM_TIMER:
        if (wParam == 1) {
            g_preview.Tick();
            CheckRetransmit();
            g_parser.CheckTimeout(GetTickCount64());
        }
        break;

    case WM_CLOSE:
        KillTimer(hDlg, 1);
        g_serial.Close();
        EndDialog(hDlg, 0);
        break;
    }
    return FALSE;
}

/* ============================================================
 * WinMain
 * ============================================================ */
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    g_hInst = hInstance;

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    // Register OLED preview window class
    OledPreview::RegisterClass(hInstance);

    DialogBox(hInstance, MAKEINTRESOURCE(IDD_OLED_CONTROL_DIALOG), nullptr, DlgProc);
    return 0;
}

