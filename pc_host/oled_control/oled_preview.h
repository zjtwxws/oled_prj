#pragma once

/**
 * @file    oled_preview.h
 * @brief   OLED 128x64 模拟预览 — 纯 Win32 GDI 控件
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

    void SetText(const char* utf8);
    void SetMode(int mode);
    void SetTime(int h, int m, int s, int y, int mo, int d, int wd);
    void SetWeather(int type, int temp, int hum, int wind);
    void SetLedState(int state);
    void Tick();

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);

    void Render();
    void DrawStatusBar(HDC dc);
    void DrawContent(HDC dc);
    void DrawStr(HDC dc, int x, int y, const char* utf8, COLORREF color, int maxX = 128);

    HWND    m_hWnd;
    HBITMAP m_hBmp;
    HDC     m_hMemDC;
    int     m_width, m_height;

    std::string m_text;
    int     m_mode;
    int     m_hour, m_min, m_sec;
    int     m_year, m_month, m_day, m_wday;
    int     m_weatherType, m_temp, m_humidity, m_wind;
    int     m_ledState;

    // Animation
    int     m_scrollX, m_scrollY;
    int     m_flipPhase;
    int     m_fadeStep, m_fadeDir;
    DWORD   m_scrollTick, m_flipTick, m_fadeTick;
};
