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
static HWND        g_hRadioRemote, g_hRadioLocal, g_hCboRemoteSub;  /* v3.1 */
static HWND        g_hRadioLed[3];
static HWND        g_hRadioMode[7];
static HWND        g_hEditText, g_hBtnSendText;
static HWND        g_hEditBootText, g_hBtnSaveBoot;
static HWND        g_hCboWeather, g_hEditTemp, g_hEditHumidity, g_hCboWind, g_hBtnSendWeather;
static HWND        g_hBtnSyncTime;
static HWND        g_hGrpWeather;        /* v3.2: 天气分组框 handle */
static HWND        g_hStcLedStatus, g_hStcModeStatus, g_hStcRemoteMode;  /* v3.1: remote mode label */
static HWND        g_hGrpKeyLog;
static HWND        g_hLstKeyLog;
static HWND        g_hOledPreview;
static HWND        g_hStatusBar;      /* v3.2: 底部状态栏 */

static SerialPort  g_serial;
static ProtocolParser g_parser;
static std::deque<TxTask> g_pendingAcks;
static uint8_t     g_seq = 0;
static bool        g_connected = false;
static bool        g_userIsRemote = false;  /* 用户 UI 意图: true=远程, false=本地 */

/* OLED preview instance */
static OledPreview g_preview;

static const wchar_t* g_modeNames[] = {
    L"静态", L"左滚", L"右滚", L"上滚", L"下滚", L"翻页", L"淡入淡出"
};

static const wchar_t* g_remoteSubNames[] = {
    L"文字", L"时间", L"天气", L"日期"
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
        /* v3.1: 2字节 [is_remote, sub_mode] */
        if (frame.len >= 2) {
            bool isRemote = frame.data[0] != 0;
            uint8_t sub = frame.data[1];

            /* 设备回传仅更新状态栏显示, 不覆盖用户意图 */
            SetWindowText(g_hStcModeStatus, isRemote ? L"远程" : L"本地");
            SetWindowText(g_hStcRemoteMode, g_remoteSubNames[sub < 4 ? sub : 0]);

            /* 如果设备回传与 UI 不一致, 以 UI 为准 (防止竞态) */
            if (g_userIsRemote == isRemote) {
                g_preview.SetRemote(isRemote);
                g_preview.SetSubMode(sub < 4 ? sub : 0);
            }
        } else if (frame.len >= 1) {
            /* 兼容旧版 1 字节 */
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
    for (size_t i = 0; i < len; i++) {
        if (g_parser.FeedByte(data[i])) {
            const ProtoFrame& f = g_parser.GetFrame();
            /* ACK 帧高频出现，不记录日志防止刷屏 */
            if (f.cmd != CMD_ACK) {
                AppendLog(L"RX: cmd=0x%02X seq=%u len=%u", f.cmd, f.seq, f.len);
            }
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

/* v3.2: 更新底部状态栏 */
static void UpdateStatusBar() {
    if (!g_hStatusBar) return;

    wchar_t text[256];
    if (g_connected) {
        /* 获取当前串口名 */
        wchar_t portName[64] = L"---";
        int sel = ComboBox_GetCurSel(g_hCboPort);
        if (sel >= 0) {
            auto ports = SerialPort::Enumerate();
            if (sel < (int)ports.size())
                wcsncpy_s(portName, ports[sel].name.c_str(), _TRUNCATE);
        }

        const wchar_t* modeStr = g_userIsRemote ? L"远程" : L"本地";
        const wchar_t* subStr = L"";
        if (g_userIsRemote) {
            int sub = ComboBox_GetCurSel(g_hCboRemoteSub);
            subStr = g_remoteSubNames[sub >= 0 && sub < 4 ? sub : 0];
        }

        _snwprintf_s(text, _TRUNCATE, L"  ● 已连接 %s  |  模式: %s %s  |  LED: %s",
            portName, modeStr, subStr,
            Button_GetCheck(g_hRadioLed[1]) ? L"开" :
            Button_GetCheck(g_hRadioLed[2]) ? L"闪烁" : L"关");
    } else {
        _snwprintf_s(text, _TRUNCATE, L"  ○ 未连接  |  请选择串口并打开");
    }
    SetWindowText(g_hStatusBar, text);
}

/* 帧缓冲分段发送 (远程模式) */
static DWORD g_lastFrameSend = 0;
static void SendFrameBuffer() {
    /* 双重守卫: 必须连接 + 用户选择了远程模式 */
    if (!g_connected || !g_userIsRemote) return;

    /* 帧率策略: TEXT 模式 25fps (40ms), TIME/WEATHER/DATE 1fps */
    DWORD interval = (g_preview.GetSubMode() == 0) ? 40 : 1000;
    DWORD now = GetTickCount();
    if (now - g_lastFrameSend < interval) return;
    g_lastFrameSend = now;

    const uint8_t* fb = g_preview.GetFrameBuffer();
    static const int PAYLOAD = 200;
    static const int TOTAL = 1024;
    int segTotal = (TOTAL + PAYLOAD - 1) / PAYLOAD;  /* 6 */

    for (int seg = 0; seg < segTotal; seg++) {
        int offset = seg * PAYLOAD;
        int remain = TOTAL - offset;
        int segLen = (remain > PAYLOAD) ? PAYLOAD : remain;

        uint8_t data[203];
        data[0] = (uint8_t)seg;
        data[1] = (uint8_t)segTotal;
        memcpy(&data[2], &fb[offset], segLen);

        SendCommand(CMD_FRAME_SYNC, data, segLen + 2);
        /* 段间留 2ms 给 STM32 处理 ACK, 防止 RX 溢出丢帧 */
        if (seg < segTotal - 1) {
            Sleep(2);
        }
    }
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

/*
 * LayoutMainDialog — 处理窗口缩放时的控件重排
 *
 * 原则:
 *   左侧面板: 所有控件位置/大小固定（来自 .rc 模板）,
 *            仅底部 4 个分组框（文字/上电/天气/同步）的 Y 坐标
 *            随窗口高度变化而平移，其内部子控件 Y 同步偏移。
 *            左侧宽度不随窗口变化，保持 RC 模板值。
 *
 *   右侧面板: OLED 预览 + 按键日志 填满剩余空间。
 */
static void LayoutMainDialog(HWND hDlg) {
    if (!g_hOledPreview || !g_hGrpKeyLog || !g_hLstKeyLog)
        return;

    RECT rcClient;
    GetClientRect(hDlg, &rcClient);
    int unitX = 100, unitY = 100;
    GetDlgUnitScale(hDlg, unitX, unitY);

    int margin   = DluToPx(10, unitX);
    int sbHeight = DluToPx(14, unitY);

    /* ======================================================
     * 右侧面板: OLED 预览 + 按键日志，填满左侧之外的空间
     * ====================================================== */
    /* 左侧固定宽度 = RC 模板中左侧控件的右边界: 220 DLU */
    int leftFixedW = DluToPx(220, unitX);
    int rightX = leftFixedW + margin;
    int rightW = rcClient.right - rightX - margin;
    if (rightW < DluToPx(180, unitX)) rightW = DluToPx(180, unitX);

    int availH = rcClient.bottom - sbHeight;
    int previewH = rightW * 64 / 128;               /* 128:64 宽高比 */
    int maxPreviewH = (availH - margin) / 3;
    if (previewH > maxPreviewH) previewH = maxPreviewH;
    if (previewH < DluToPx(64, unitY)) previewH = DluToPx(64, unitY);
    SetWindowPos(g_hOledPreview, nullptr, rightX, margin, rightW, previewH, SWP_NOZORDER);

    int logTop   = margin + previewH + margin;
    int logH     = availH - logTop;
    if (logH < DluToPx(60, unitY)) logH = DluToPx(60, unitY);
    SetWindowPos(g_hGrpKeyLog, nullptr, rightX, logTop, rightW, logH, SWP_NOZORDER);

    int innerM   = DluToPx(8, unitX);
    int listTop  = logTop + DluToPx(18, unitY);
    SetWindowPos(g_hLstKeyLog, nullptr,
                 rightX + innerM, listTop,
                 rightW - 2 * innerM, logH - DluToPx(26, unitY), SWP_NOZORDER);

    /* ======================================================
     * 左侧底部 4 个分组框: Y 随窗口高度平移
     * ====================================================== */
    /* 左侧上部固定区域底部边界: "显示特效" 分组框的底边 */
    int fixedBottom = 0;
    for (int i = 0; i < 7; i++) {
        if (g_hRadioMode[i]) {
            RECT r;
            if (GetWindowRect(g_hRadioMode[i], &r)) {
                MapWindowPoints(HWND_DESKTOP, hDlg, (POINT*)&r, 2);
                if (r.bottom > fixedBottom) fixedBottom = r.bottom;
            }
        }
    }
    if (fixedBottom < DluToPx(280, unitY)) fixedBottom = DluToPx(280, unitY);

    int availBottom = availH - margin;
    int totalBelow  = availBottom - fixedBottom - margin;
    if (totalBelow < DluToPx(80, unitY)) totalBelow = DluToPx(80, unitY);

    /* 4 组固定逻辑高度（DLU）: 文字65, 上电50, 天气60, 同步22 */
    int rawDLU[4] = { 65, 50, 60, 22 };
    int rawTotalDLU = rawDLU[0] + rawDLU[1] + rawDLU[2] + rawDLU[3] + 4 * 10;  /* 含间隙 */
    float yScale = (float)totalBelow / (float)DluToPx(rawTotalDLU, unitY);
    if (yScale > 1.0f) yScale = 1.0f;

    HWND hGrpText = GetDlgItem(hDlg, IDC_GRP_TEXT);
    HWND hGrpBoot = GetDlgItem(hDlg, IDC_GRP_SAVE_BOOT);
    HWND hGrpWth  = g_hGrpWeather;
    HWND hBtnSync = g_hBtnSyncTime;

    /* ===== 记录原始的 RC 模板 Y 坐标 (计算 delta) ===== */
    RECT rcGrpTextOrig, rcGrpBootOrig, rcGrpWthOrig, rcBtnSyncOrig;
    /* 模板值来自 .rc:
       IDC_GRP_TEXT        y=295 h=65
       IDC_GRP_SAVE_BOOT   y=370 h=50
       IDC_GRP_WEATHER     y=430 h=60
       IDC_BTN_SYNC_TIME   y=497 h=22  */

    /* 先获取当前屏幕坐标 */
    GetWindowRect(hGrpText,  &rcGrpTextOrig);
    GetWindowRect(hGrpBoot,  &rcGrpBootOrig);
    GetWindowRect(hGrpWth,   &rcGrpWthOrig);
    GetWindowRect(hBtnSync,  &rcBtnSyncOrig);

    int textOrigY = 295, bootOrigY = 370, wthOrigY = 430, syncOrigY = 497;
    int textOrigH = 65,  bootOrigH = 50,  wthOrigH  = 60,  syncOrigH  = 22;

    int dluToPxY = unitY;  /* 每 100 DLU = unitY 像素, 即 1 DLU = unitY/100 px */

    /* Y 从 fixedBottom + margin 开始放 */
    int cy = fixedBottom + margin;
    int gap = (int)(DluToPx(10, unitX) * yScale);
    if (gap < DluToPx(3, unitX)) gap = DluToPx(3, unitX);

    /* 文字内容 */
    int h1 = (int)(DluToPx(textOrigH, unitY) * yScale);
    if (h1 < DluToPx(30, unitY)) h1 = DluToPx(30, unitY);
    /* 按比例缩放组框宽度不起作用，因为 yScale 只影响高度 */
    int textGrpW = DluToPx(200, unitX);
    SetWindowPos(hGrpText, nullptr, DluToPx(10, unitX), cy, textGrpW, h1, SWP_NOZORDER);

    /* 文字内容子控件: 编辑框 + 发送按钮，Y 跟随分组框 */
    int editH = h1 - DluToPx(30, unitY);
    if (editH < DluToPx(14, unitY)) editH = DluToPx(14, unitY);
    SetWindowPos(g_hEditText, nullptr, DluToPx(20, unitX), cy + DluToPx(18, unitY),
                 DluToPx(180, unitX), editH, SWP_NOZORDER);
    SetWindowPos(g_hBtnSendText, nullptr, DluToPx(150, unitX),
                 cy + h1 - DluToPx(18, unitY),
                 DluToPx(50, unitX), DluToPx(16, unitY), SWP_NOZORDER);

    cy += h1 + gap;

    /* 上电文字 */
    int h2 = (int)(DluToPx(bootOrigH, unitY) * yScale);
    if (h2 < DluToPx(25, unitY)) h2 = DluToPx(25, unitY);
    SetWindowPos(hGrpBoot, nullptr, DluToPx(10, unitX), cy, textGrpW, h2, SWP_NOZORDER);
    SetWindowPos(g_hEditBootText, nullptr, DluToPx(20, unitX), cy + DluToPx(18, unitY),
                 DluToPx(140, unitX), DluToPx(14, unitY), SWP_NOZORDER);
    SetWindowPos(g_hBtnSaveBoot, nullptr, DluToPx(170, unitX), cy + DluToPx(17, unitY),
                 DluToPx(35, unitX), DluToPx(16, unitY), SWP_NOZORDER);

    cy += h2 + gap;

    /* 天气 */
    int h3 = (int)(DluToPx(wthOrigH, unitY) * yScale);
    if (h3 < DluToPx(30, unitY)) h3 = DluToPx(30, unitY);
    SetWindowPos(hGrpWth, nullptr, DluToPx(10, unitX), cy, textGrpW, h3, SWP_NOZORDER);

    int rowH = DluToPx(14, unitY);
    if (h3 < 2 * rowH + DluToPx(4, unitY)) {
        rowH = (h3 - DluToPx(8, unitY)) / 2;
        if (rowH < DluToPx(10, unitY)) rowH = DluToPx(10, unitY);
    }
    int wy1 = cy + DluToPx(6, unitY);
    int wy2 = wy1 + rowH + DluToPx(2, unitY);

    /* 第1行 */
    SetWindowPos(GetDlgItem(hDlg, IDC_STC_WTH_TYPE), nullptr,
                 DluToPx(20, unitX), wy1, DluToPx(30, unitX), rowH, SWP_NOZORDER);
    SetWindowPos(g_hCboWeather, nullptr,
                 DluToPx(50, unitX), wy1 - 1, DluToPx(60, unitX), DluToPx(100, unitY), SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hDlg, IDC_STC_WTH_TEMP), nullptr,
                 DluToPx(120, unitX), wy1, DluToPx(30, unitX), rowH, SWP_NOZORDER);
    SetWindowPos(g_hEditTemp, nullptr,
                 DluToPx(152, unitX), wy1 - 1, DluToPx(30, unitX), rowH, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hDlg, IDC_STC_WTH_CELSIUS), nullptr,
                 DluToPx(185, unitX), wy1, DluToPx(12, unitX), rowH, SWP_NOZORDER);
    /* 第2行 */
    SetWindowPos(GetDlgItem(hDlg, IDC_STC_WTH_HUMID), nullptr,
                 DluToPx(20, unitX), wy2, DluToPx(30, unitX), rowH, SWP_NOZORDER);
    SetWindowPos(g_hEditHumidity, nullptr,
                 DluToPx(50, unitX), wy2 - 1, DluToPx(30, unitX), rowH, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hDlg, IDC_STC_WTH_PERCENT), nullptr,
                 DluToPx(83, unitX), wy2, DluToPx(12, unitX), rowH, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hDlg, IDC_STC_WTH_WIND), nullptr,
                 DluToPx(100, unitX), wy2, DluToPx(30, unitX), rowH, SWP_NOZORDER);
    SetWindowPos(g_hCboWind, nullptr,
                 DluToPx(132, unitX), wy2 - 1, DluToPx(50, unitX), DluToPx(100, unitY), SWP_NOZORDER);
    SetWindowPos(g_hBtnSendWeather, nullptr,
                 DluToPx(190, unitX), wy2, DluToPx(18, unitX), rowH, SWP_NOZORDER);

    cy += h3 + gap;

    /* 同步时间 */
    int h4 = DluToPx(syncOrigH, unitY);
    SetWindowPos(hBtnSync, nullptr, DluToPx(10, unitX), cy, textGrpW, h4, SWP_NOZORDER);

    /* ===== 底部状态栏 ===== */
    if (g_hStatusBar)
        SetWindowPos(g_hStatusBar, nullptr, 0, rcClient.bottom - sbHeight,
                     rcClient.right, sbHeight, SWP_NOZORDER);

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
        g_hRadioRemote  = GetDlgItem(hDlg, IDC_RADIO_REMOTE);      /* v3.1 */
        g_hRadioLocal   = GetDlgItem(hDlg, IDC_RADIO_LOCAL);       /* v3.1 */
        g_hCboRemoteSub = GetDlgItem(hDlg, IDC_CBO_REMOTE_SUB);   /* v3.1 */
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
        g_hStcRemoteMode = GetDlgItem(hDlg, IDC_STC_REMOTE_MODE);  /* v3.1 */
        g_hLstKeyLog    = GetDlgItem(hDlg, IDC_LST_KEY_LOG);
        g_hGrpKeyLog    = GetDlgItem(hDlg, IDC_GRP_KEY_LOG);
        g_hGrpWeather   = GetDlgItem(hDlg, IDC_GRP_WEATHER);  /* v3.2 */
        g_hOledPreview  = GetDlgItem(hDlg, IDC_OLED_PREVIEW);

        // v3.2: Create status bar
        g_hStatusBar = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0, 0, 0, 0, hDlg, (HMENU)IDC_STATUS_BAR, g_hInst, nullptr);

        // Init controls
        Button_SetCheck(g_hRadioLocal, BST_CHECKED);  /* v3.1: default local */
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

        // Remote sub mode combo (v3.1)
        for (int i = 0; i < 4; i++) ComboBox_AddString(g_hCboRemoteSub, g_remoteSubNames[i]);
        ComboBox_SetCurSel(g_hCboRemoteSub, 0);
        SetWindowText(g_hStcRemoteMode, L"远程: 文字");

        PopulateComPorts();
        SetWindowText(g_hStcConn, L"○ 未连接");
        EnableWindow(g_hBtnClose, FALSE);

        // Init OLED preview
        g_preview.Attach(g_hOledPreview);
        LayoutMainDialog(hDlg);
        UpdateStatusBar();  /* v3.2 */

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
                    UpdateStatusBar();  /* v3.2 */
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
            UpdateStatusBar();  /* v3.2 */
            AppendLog(L"串口已断开");
            break;

        case IDC_RADIO_LED_OFF: case IDC_RADIO_LED_ON: case IDC_RADIO_LED_BLINK: {
            uint8_t state = 0;
            if (Button_GetCheck(g_hRadioLed[1]) == BST_CHECKED) state = 1;
            else if (Button_GetCheck(g_hRadioLed[2]) == BST_CHECKED) state = 2;
            SendCommand(CMD_LED_CTRL, &state, 1);
            g_preview.SetLedState(state);
            UpdateStatusBar();  /* v3.2 */
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

        // v3.1: Local/Remote radio
        case IDC_RADIO_LOCAL:
        case IDC_RADIO_REMOTE:
            if (HIWORD(wParam) == BN_CLICKED) {
                bool isRemote = Button_GetCheck(g_hRadioRemote) == BST_CHECKED;
                g_userIsRemote = isRemote;  /* 记录用户意图 */
                int sub = ComboBox_GetCurSel(g_hCboRemoteSub);
                if (sub < 0) sub = 0;
                /* data[0]: bit7=1 远程, bit7=0 本地 */
                uint8_t d;
                if (isRemote) {
                    d = (uint8_t)(0x80 | sub);
                } else {
                    d = 0;
                    for (int i = 0; i < 7; i++) {
                        if (Button_GetCheck(g_hRadioMode[i]) == BST_CHECKED) { d = (uint8_t)i; break; }
                    }
                }
                SendCommand(CMD_DISPLAY_MODE, &d, 1);
                g_preview.SetRemote(isRemote);
                g_preview.SetSubMode(sub);
                UpdateStatusBar();  /* v3.2 */
            }
            break;

        // v3.1: Remote sub mode combo
        case IDC_CBO_REMOTE_SUB:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                int sub = ComboBox_GetCurSel(g_hCboRemoteSub);
                if (sub >= 0 && sub < 4) {
                    bool isRemote = Button_GetCheck(g_hRadioRemote) == BST_CHECKED;
                    g_userIsRemote = isRemote;  /* 记录用户意图 */
                    uint8_t d = isRemote ? (uint8_t)(0x80 | sub) : (uint8_t)sub;
                    SendCommand(CMD_DISPLAY_MODE, &d, 1);
                    SetWindowText(g_hStcRemoteMode, g_remoteSubNames[sub]);
                    g_preview.SetRemote(isRemote);
                    g_preview.SetSubMode(sub);
                    UpdateStatusBar();  /* v3.2 */
                }
            }
            break;

        // Mode radios
        case IDC_RADIO_MODE_BASE: case IDC_RADIO_MODE_BASE+1: case IDC_RADIO_MODE_BASE+2:
        case IDC_RADIO_MODE_BASE+3: case IDC_RADIO_MODE_BASE+4:
        case IDC_RADIO_MODE_BASE+5: case IDC_RADIO_MODE_BASE+6:
            if (HIWORD(wParam) == BN_CLICKED) {
                for (int i = 0; i < 7; i++) {
                    if (Button_GetCheck(g_hRadioMode[i]) == BST_CHECKED) {
                        bool isRemote = Button_GetCheck(g_hRadioRemote) == BST_CHECKED;
                        g_userIsRemote = isRemote;  /* 记录用户意图 */
                        int sub = ComboBox_GetCurSel(g_hCboRemoteSub);
                        if (sub < 0) sub = 0;
                        uint8_t d = isRemote ? (uint8_t)(0x80 | sub) : (uint8_t)i;
                        SendCommand(CMD_DISPLAY_MODE, &d, 1);
                        g_preview.SetMode(i);
                        UpdateStatusBar();  /* v3.2 */
                        break;
                    }
                }
            }
            break;
        }
        break;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        RECT rcMin = { 0, 0, 620, 580 };
        MapDialogRect(hDlg, &rcMin);
        mmi->ptMinTrackSize.x = rcMin.right;
        mmi->ptMinTrackSize.y = rcMin.bottom;
        return 0;
    }

    case WM_SIZE:
        LayoutMainDialog(hDlg);
        break;


    case WM_TIMER:
        if (wParam == 1) {
            g_preview.Tick();
            SendFrameBuffer();       /* v3.1: 远程模式帧缓冲发送 */
            CheckRetransmit();
            g_parser.CheckTimeout(GetTickCount64());
        }
        break;

    case WM_CLOSE:
        KillTimer(hDlg, 1);
        g_serial.Close();
        DestroyWindow(hDlg);
        return TRUE;

    case WM_DESTROY:
        PostQuitMessage(0);
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

    // 非模态对话框: 创建后进入标准消息循环, 支持窗口拖拽缩放
    HWND hDlg = CreateDialogParamW(hInstance, MAKEINTRESOURCE(IDD_OLED_CONTROL_DIALOG),
        nullptr, DlgProc, 0);
    if (!hDlg) {
        MessageBoxW(nullptr, L"创建主窗口失败", L"错误", MB_ICONERROR);
        return 1;
    }

    ShowWindow(hDlg, nCmdShow);
    UpdateWindow(hDlg);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}

