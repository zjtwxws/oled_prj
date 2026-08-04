#pragma once

/**
 * @file    oled_preview.h
 * @brief   OLED 128x64 模拟预览 — 本地/远程双模式 (v3.1)
 *
 * 本地模式: GDI 文字+7种特效全屏渲染
 * 远程模式: 渲染到帧缓冲 → GDI显示 + 供 CMD_FRAME_SYNC 分段发送
 */

#include <windows.h>
#include <string>
#include <cstdint>

#define OLED_PREVIEW_CLASS L"OledPreviewWnd"

class OledPreview {
public:
    OledPreview();
    ~OledPreview();

    static void RegisterClass(HINSTANCE hInst);

    void Attach(HWND hWnd);
    HWND GetHwnd() const { return m_hWnd; }

    /* 状态设置 */
    void SetText(const char* utf8);
    void SetMode(int mode);           /* 特效模式 (本地/远程文字) */
    void SetTime(int h, int m, int s, int y, int mo, int d, int wd);
    void SetWeather(int type, int temp, int hum, int wind);
    void SetLedState(int state);
    void SetRemote(bool remote);
    void SetSubMode(int sm);          /* 远程子模式: 0=TEXT,1=TIME,2=WEATHER,3=DATE */

    bool IsRemote() const { return m_isRemote; }
    int  GetSubMode() const { return m_subMode; }

    /* 获取渲染好的帧缓冲 (远程模式 PC→STM 下发用) */
    const uint8_t* GetFrameBuffer() const { return m_frameBuf; }
    static constexpr int FrameBufSize = 1024;

    /* 动画 tick (50ms) */
    void Tick();

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);

    void Render();
    void RenderLocal();
    void RenderRemote();

    /* 本地模式: 文字+特效 */
    void DrawStr(HDC dc, int x, int y, const char* utf8, COLORREF color, int maxX = 128);
    int  GetStrPixelWidth(const char* utf8);

    /* 远程模式: 各子模式渲染 */
    void RenderTimeMode();
    void RenderWeatherMode();
    void RenderDateMode();
    void DrawBigDigit(HDC dc, int x, int y, int digit, COLORREF color);

    /* 帧缓冲像素设置 (远程模式) */
    void SetPixelToBuf(int x, int y, bool on);

    /* 清除帧缓冲 */
    void ClearFrameBuf();

    HWND    m_hWnd;
    HBITMAP m_hBmp;
    HDC     m_hMemDC;
    int     m_width, m_height;

    /* 模式 */
    bool    m_isRemote;
    int     m_subMode;       /* 远程子模式 */

    /* 文字 */
    std::string m_text;
    int     m_mode;           /* 特效模式 */

    /* 时间 */
    int     m_hour, m_min, m_sec;
    int     m_year, m_month, m_day, m_wday;

    /* 天气 */
    int     m_weatherType, m_temp, m_humidity, m_wind;

    /* LED */
    int     m_ledState;

    /* 帧缓冲 (远程模式) */
    uint8_t m_frameBuf[1024];

    /* 动画 */
    int     m_scrollX, m_scrollY;
    int     m_flipPhase;
    int     m_fadeStep, m_fadeDir;
    DWORD   m_scrollTick, m_flipTick, m_fadeTick;
};
