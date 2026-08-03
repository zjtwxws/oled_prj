/**
 * @file    oled_preview.cpp
 * @brief   OLED 128x64 模拟预览 — 纯 Win32 GDI 实现
 */

#include "oled_preview.h"
#include "oled_preview_gen.h"
#include <algorithm>
#include <cstdio>

#define OLED_W 128
#define OLED_H 64

static const COLORREF BG_COLOR = RGB(32, 32, 32);
static const COLORREF PX_COLOR = RGB(180, 255, 80);

static const char* weather_names[] = {
    "\xe6\x99\xb4", "\xe5\xa4\x9a\xe4\xba\x91", "\xe9\x98\xb4",
    "\xe5\xb0\x8f\xe9\x9b\xa8", "\xe5\xa4\xa7\xe9\x9b\xa8",
    "\xe9\x9b\xb7\xe9\x9b\xa8", "\xe9\x9b\xaa"
};

static HINSTANCE g_previewInst = nullptr;

/* ---- Registry ---- */
void OledPreview::RegisterClass(HINSTANCE hInst) {
    g_previewInst = hInst;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = OLED_PREVIEW_CLASS;
    RegisterClassExW(&wc);
}

OledPreview::OledPreview()
    : m_hWnd(nullptr), m_hBmp(nullptr), m_hMemDC(nullptr)
    , m_width(OLED_W), m_height(OLED_H)
    , m_mode(0), m_hour(0), m_min(0), m_sec(0)
    , m_year(2025), m_month(1), m_day(1), m_wday(0)
    , m_weatherType(0), m_temp(25), m_humidity(65), m_wind(0)
    , m_ledState(0)
    , m_scrollX(0), m_scrollY(0), m_flipPhase(0)
    , m_fadeStep(0), m_fadeDir(1)
    , m_scrollTick(0), m_flipTick(0), m_fadeTick(0)
{
    m_text = "\xe6\xac\xa2\xe8\xbf\x8e\xe8\xbf\x9b\xe5\x85\xa5\xe7\xb3\xbb\xe7\xbb\x9f";
}

OledPreview::~OledPreview() {
    if (m_hMemDC) { DeleteDC(m_hMemDC); m_hMemDC = nullptr; }
    if (m_hBmp) { DeleteObject(m_hBmp); m_hBmp = nullptr; }
}

void OledPreview::Attach(HWND hWnd) {
    m_hWnd = hWnd;
    HDC hdc = GetDC(hWnd);
    m_hMemDC = CreateCompatibleDC(hdc);
    m_hBmp = CreateCompatibleBitmap(hdc, OLED_W, OLED_H);
    SelectObject(m_hMemDC, m_hBmp);
    ReleaseDC(hWnd, hdc);
    Render();
}

/* ---- Drawing primitives ---- */
static void DrawPx(HDC dc, int x, int y, COLORREF c) {
    if (x >= 0 && x < OLED_W && y >= 0 && y < OLED_H) SetPixel(dc, x, y, c);
}

static void DrawChar(HDC dc, int x, int y, const uint8_t* g, int w, int h, COLORREF c) {
    for (int col = 0; col < w; col++)
        for (int row = 0; row < h; row++)
            if (g[col + (row / 8) * w] & (1 << (row % 8)))
                DrawPx(dc, x + col, y + row, c);
}

void OledPreview::DrawStr(HDC dc, int x, int y, const char* utf8, COLORREF color, int maxX) {
    int cx = x;
    while (*utf8 && cx < maxX) {
        unsigned char c = (unsigned char)*utf8;
        if (c < 0x80) {
            if (cx + 8 > maxX) break;
            DrawChar(dc, cx, y, get_ascii_glyph((char)c), 8, 16, color);
            cx += 8; utf8++;
        } else if ((c & 0xE0) == 0xC0) { utf8 += 2; }
        else if ((c & 0xF0) == 0xE0) {
            if (cx + 16 > maxX) break;
            const uint8_t* g = get_chinese_glyph(utf8);
            if (g) DrawChar(dc, cx, y, g, 16, 16, color);
            cx += 16; utf8 += 3;
        } else { utf8 += 4; }
    }
}

/* ---- Render ---- */
void OledPreview::Render() {
    if (!m_hMemDC) return;
    RECT rc = {0, 0, OLED_W, OLED_H};
    HBRUSH bg = CreateSolidBrush(BG_COLOR);
    FillRect(m_hMemDC, &rc, bg);
    DeleteObject(bg);

    DrawStatusBar(m_hMemDC);
    DrawContent(m_hMemDC);
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void OledPreview::DrawStatusBar(HDC dc) {
    char buf[32];
    sprintf_s(buf, "%02d:%02d", m_hour, m_min);
    DrawStr(dc, 0, 0, buf, PX_COLOR);

    std::string wt(weather_names[m_weatherType]);
    sprintf_s(buf, " %dC", m_temp);
    wt += buf;
    DrawStr(dc, 72, 0, wt.c_str(), PX_COLOR);

    const char* led = (m_ledState == 0) ? "[]" : (m_ledState == 1) ? "[*]" : "[~]";
    DrawStr(dc, 110, 0, led, PX_COLOR);
}

void OledPreview::DrawContent(HDC dc) {
    int cy = 16;
    int tw = get_str_pixel_width(m_text.c_str());
    COLORREF color = PX_COLOR;

    if (m_mode == 6) { // Fade
        int dim = (m_fadeStep * 255) / 32;
        if (dim < 8) dim = 8;
        color = RGB((GetRValue(PX_COLOR)*dim)/255,
                    (GetGValue(PX_COLOR)*dim)/255,
                    (GetBValue(PX_COLOR)*dim)/255);
    }

    switch (m_mode) {
    case 0: DrawStr(dc, 0, cy, m_text.c_str(), color); break;
    case 1: DrawStr(dc, -m_scrollX, cy, m_text.c_str(), color, OLED_W); break;
    case 2: DrawStr(dc, m_scrollX - tw, cy, m_text.c_str(), color, OLED_W); break;
    case 3: DrawStr(dc, 0, cy - m_scrollY, m_text.c_str(), color); break;
    case 4: DrawStr(dc, 0, cy + m_scrollY, m_text.c_str(), color); break;
    case 5: DrawStr(dc, 0, cy + (m_flipPhase ? 8 : 0), m_text.c_str(), color); break;
    case 6: DrawStr(dc, 0, cy, m_text.c_str(), color); break;
    default: DrawStr(dc, 0, cy, m_text.c_str(), color); break;
    }
}

/* ---- State setters ---- */
void OledPreview::SetText(const char* t) { m_text = t ? t : ""; Render(); }
void OledPreview::SetMode(int m) {
    if (m >= 0 && m < 7) { m_mode = m; m_scrollX = m_scrollY = 0; m_fadeStep = 0; m_fadeDir = 1; Render(); }
}
void OledPreview::SetTime(int h, int m, int s, int y, int mo, int d, int wd)
{ m_hour=h; m_min=m; m_sec=s; m_year=y; m_month=mo; m_day=d; m_wday=wd; Render(); }
void OledPreview::SetWeather(int t, int tmp, int h, int w)
{ m_weatherType=t; m_temp=tmp; m_humidity=h; m_wind=w; Render(); }
void OledPreview::SetLedState(int s) { m_ledState = s; Render(); }

/* ---- Animation tick ---- */
void OledPreview::Tick() {
    DWORD now = GetTickCount();
    int tw = get_str_pixel_width(m_text.c_str());

    switch (m_mode) {
    case 1: case 2: // scroll L/R
        if (now - m_scrollTick >= 40) {
            m_scrollTick = now; m_scrollX += 2;
            if (m_scrollX > tw + OLED_W) m_scrollX = 0;
            Render();
        }
        break;
    case 3: case 4: // scroll U/D
        if (now - m_scrollTick >= 40) {
            m_scrollTick = now; m_scrollY += 2;
            if (m_scrollY > 48) m_scrollY = 0;
            Render();
        }
        break;
    case 5: // flip
        if (now - m_flipTick >= 3000) {
            m_flipTick = now; m_flipPhase ^= 1; Render();
        }
        break;
    case 6: // fade
        if (now - m_fadeTick >= 30) {
            m_fadeTick = now;
            if (m_fadeDir) { m_fadeStep++; if (m_fadeStep >= 32) m_fadeDir = 0; }
            else { m_fadeStep--; if (m_fadeStep <= 0) m_fadeDir = 1; }
            Render();
        }
        break;
    }
}

/* ---- WndProc ---- */
LRESULT CALLBACK OledPreview::WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    OledPreview* self = (OledPreview*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (self && self->m_hMemDC) {
            RECT rc; GetClientRect(hWnd, &rc);
            float s = (std::min)((float)rc.right / OLED_W, (float)rc.bottom / OLED_H);
            int fw = (int)(OLED_W * s), fh = (int)(OLED_H * s);
            int ox = (rc.right - fw) / 2, oy = (rc.bottom - fh) / 2;
            HBRUSH b = CreateSolidBrush(RGB(16,16,16));
            FillRect(hdc, &rc, b);
            DeleteObject(b);
            SetStretchBltMode(hdc, COLORONCOLOR);
            StretchBlt(hdc, ox, oy, fw, fh, self->m_hMemDC, 0, 0, OLED_W, OLED_H, SRCCOPY);
        }
        EndPaint(hWnd, &ps);
        return 0;
    }
    }
    return DefWindowProc(hWnd, msg, wp, lp);
}
