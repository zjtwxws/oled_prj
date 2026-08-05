/**
 * @file    oled_preview.cpp
 * @brief   OLED 128x64 模拟预览 — 本地/远程双模式 GDI 实现 (v3.1)
 */

#include "oled_preview.h"
#include "oled_preview_gen.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

/* 告诉 MSVC 窄字符串字面量按 UTF-8 解释, 解决 C4819 警告 */
#if defined(_MSC_VER)
#pragma execution_character_set("utf-8")
#endif

#define OLED_W 128
#define OLED_H 64

static const COLORREF BG_COLOR = RGB(32, 32, 32);
static const COLORREF PX_COLOR = RGB(180, 255, 80);

static const char* weather_names[] = {
    "晴", "多云", "阴",
    "小雨", "大雨",
    "雷雨", "雪"
};

/* 天气图标: 8×8 点阵 (Sun,Cloud,Overcast,Rain,Heavy Rain,Thunder,Snow) */
static const uint8_t weather_icons_8x8[7][8] = {
    {0x3C,0x42,0x99,0xA5,0x99,0x42,0x3C,0x00}, // Sun
    {0x30,0x4C,0x42,0x30,0x0C,0x30,0x40,0x00}, // Cloud
    {0x38,0x44,0x38,0x44,0x38,0x00,0x00,0x00}, // Overcast
    {0x08,0x14,0x08,0x24,0x52,0x24,0x00,0x00}, // Light Rain
    {0x1C,0x22,0x1C,0x26,0x59,0x26,0x00,0x00}, // Heavy Rain
    {0x1C,0x22,0x1C,0x08,0x7F,0x08,0x14,0x22}, // Thunder
    {0x24,0x5A,0x24,0x18,0x24,0x5A,0x24,0x18}, // Snow
};

/* 星期中文名 */
static const char* week_names[] = {
    "日", "一", "二", "三",
    "四", "五", "六"
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
    , m_isRemote(false), m_subMode(0)
    , m_mode(0), m_hour(0), m_min(0), m_sec(0)
    , m_year(2025), m_month(1), m_day(1), m_wday(0)
    , m_weatherType(0), m_temp(25), m_humidity(65), m_wind(0)
    , m_ledState(0)
    , m_scrollX(0), m_scrollY(0), m_flipPhase(0)
    , m_fadeStep(0), m_fadeDir(1)
    , m_scrollTick(0), m_flipTick(0), m_fadeTick(0)
{
    m_text = "欢迎进入系统";
    memset(m_frameBuf, 0, sizeof(m_frameBuf));
}

OledPreview::~OledPreview() {
    if (m_hMemDC) { DeleteDC(m_hMemDC); m_hMemDC = nullptr; }
    if (m_hBmp) { DeleteObject(m_hBmp); m_hBmp = nullptr; }
}

void OledPreview::Attach(HWND hWnd) {
    m_hWnd = hWnd;
    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)this);
    HDC hdc = GetDC(hWnd);
    m_hMemDC = CreateCompatibleDC(hdc);
    m_hBmp = CreateCompatibleBitmap(hdc, OLED_W, OLED_H);
    SelectObject(m_hMemDC, m_hBmp);
    ReleaseDC(hWnd, hdc);
    Render();
}

/* ---- 帧缓冲操作 ---- */
void OledPreview::ClearFrameBuf() {
    memset(m_frameBuf, 0, sizeof(m_frameBuf));
}

void OledPreview::SetPixelToBuf(int x, int y, bool on) {
    if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) return;
    uint8_t page = y >> 3;
    uint8_t bit = 1 << (y & 0x07);
    if (on)
        m_frameBuf[page * OLED_W + x] |= bit;
    else
        m_frameBuf[page * OLED_W + x] &= ~bit;
}

/* ---- GDI 绘制基元 ---- */
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
            if (g) {
                /* 中文字形为 STM32 交错格式: col0_upper, col0_lower, col1_upper, ... */
                for (int col = 0; col < 16; col++) {
                    for (int row = 0; row < 16; row++) {
                        int byteIdx = col * 2 + (row / 8);
                        if (g[byteIdx] & (1 << (row % 8)))
                            DrawPx(dc, cx + col, y + row, color);
                    }
                }
            }
            cx += 16; utf8 += 3;
        } else { utf8 += 4; }
    }
}

/*
 * 自动换行版 DrawStr: 文字超出 maxX 时跳到下一行继续绘制。
 * 用于 TIME/WEATHER/DATE 等静态文字模式 (不用于滚动特效)。
 */
static void DrawStrWrap(HDC dc, int x, int y, const char* utf8, COLORREF color, int maxX) {
    const int LINE_H = 16;
    int cx = x;
    while (*utf8 && y + LINE_H <= OLED_H) {
        unsigned char c = (unsigned char)*utf8;
        int cw;

        if (c < 0x80) {
            cw = 8;
        } else if ((c & 0xE0) == 0xC0) {
            utf8 += 2; continue;
        } else if ((c & 0xF0) == 0xE0) {
            cw = 16;
        } else {
            utf8 += 4; continue;
        }

        /* 当前字符放不下 → 换行 */
        if (cx + cw > maxX) {
            cx = x;
            y += LINE_H;
            if (y + LINE_H > OLED_H) break;
            continue;
        }

        if (c < 0x80) {
            DrawChar(dc, cx, y, get_ascii_glyph((char)c), 8, 16, color);
            cx += 8; utf8++;
        } else {
            const uint8_t* g = get_chinese_glyph(utf8);
            if (g) {
                for (int col = 0; col < 16; col++) {
                    for (int row = 0; row < 16; row++) {
                        int byteIdx = col * 2 + (row / 8);
                        if (g[byteIdx] & (1 << (row % 8)))
                            DrawPx(dc, cx + col, y + row, color);
                    }
                }
            }
            cx += 16; utf8 += 3;
        }
    }
}

int OledPreview::GetStrPixelWidth(const char* utf8) {
    int w = 0;
    const char* p = utf8;
    while (*p) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x80) { w += 8; p++; }
        else if ((c & 0xF0) == 0xE0) { w += 16; p += 3; }
        else if ((c & 0xE0) == 0xC0) { p += 2; }
        else { p += 4; }
    }
    return w;
}

/* 大字数字绘制 (16×24 像素放大) */
void OledPreview::DrawBigDigit(HDC dc, int x, int y, int digit, COLORREF color) {
    const char* num[10] = {"0","1","2","3","4","5","6","7","8","9"};
    if (digit < 0 || digit > 9) return;
    DrawStr(dc, x, y, num[digit], color);  /* 简化为 8×16 ASCII 居中变体 */
}

/* ---- 主渲染入口 ---- */
void OledPreview::Render() {
    if (!m_hMemDC) return;
    if (m_isRemote) {
        RenderRemote();
    } else {
        RenderLocal();
    }
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

/* ---- 本地模式渲染 ---- */
void OledPreview::RenderLocal() {
    RECT rc = {0, 0, OLED_W, OLED_H};
    HBRUSH bg = CreateSolidBrush(BG_COLOR);
    FillRect(m_hMemDC, &rc, bg);
    DeleteObject(bg);

    int tw = GetStrPixelWidth(m_text.c_str());
    COLORREF color = PX_COLOR;

    if (m_mode == 6) {
        int dim = (m_fadeStep * 255) / 32;
        if (dim < 8) dim = 8;
        color = RGB((GetRValue(PX_COLOR)*dim)/255,
                    (GetGValue(PX_COLOR)*dim)/255,
                    (GetBValue(PX_COLOR)*dim)/255);
    }

    switch (m_mode) {
    case 0: /* 静态: 自动换行 */
        DrawStrWrap(m_hMemDC, 0, 0, m_text.c_str(), color, OLED_W);
        break;
    case 1: /* 左滚: 完整文字无截断, 视口自然裁剪 */
        DrawStr(m_hMemDC, -m_scrollX, 0, m_text.c_str(), color, 9999);
        break;
    case 2: /* 右滚 */
        DrawStr(m_hMemDC, m_scrollX - tw, 0, m_text.c_str(), color, 9999);
        break;
    case 3: /* 上滚 */
        DrawStr(m_hMemDC, 0, -m_scrollY, m_text.c_str(), color, 9999);
        break;
    case 4: /* 下滚 */
        DrawStr(m_hMemDC, 0, m_scrollY, m_text.c_str(), color, 9999);
        break;
    case 5: /* 翻页 */
        DrawStrWrap(m_hMemDC, 0, m_flipPhase ? 8 : 0, m_text.c_str(), color, OLED_W);
        break;
    case 6: /* 淡入淡出 */
        DrawStrWrap(m_hMemDC, 0, 0, m_text.c_str(), color, OLED_W);
        break;
    default:
        DrawStrWrap(m_hMemDC, 0, 0, m_text.c_str(), color, OLED_W);
        break;
    }
}

/* ---- 远程模式渲染 (渲染到 m_frameBuf + GDI 显示) ---- */
void OledPreview::RenderRemote() {
    ClearFrameBuf();

    switch (m_subMode) {
    case 0: /* TEXT — 与本地相同 */
        RenderLocal();
        /* 同步到帧缓冲 */
        for (int y = 0; y < OLED_H; y++)
            for (int x = 0; x < OLED_W; x++)
                if (GetPixel(m_hMemDC, x, y) != BG_COLOR)
                    SetPixelToBuf(x, y, true);
        return;

    case 1: RenderTimeMode(); break;
    case 2: RenderWeatherMode(); break;
    case 3: RenderDateMode(); break;
    }

    /* 帧缓冲 → GDI 显示 */
    RECT rc = {0, 0, OLED_W, OLED_H};
    HBRUSH bg = CreateSolidBrush(BG_COLOR);
    FillRect(m_hMemDC, &rc, bg);
    DeleteObject(bg);
    for (int y = 0; y < OLED_H; y++)
        for (int x = 0; x < OLED_W; x++)
            if (m_frameBuf[(y >> 3) * OLED_W + x] & (1 << (y & 7)))
                DrawPx(m_hMemDC, x, y, PX_COLOR);
}

/* ---- 远程子模式: TIME ---- */
void OledPreview::RenderTimeMode() {
    RECT rc = {0, 0, OLED_W, OLED_H};
    HBRUSH bg = CreateSolidBrush(BG_COLOR);
    FillRect(m_hMemDC, &rc, bg);
    DeleteObject(bg);

    /* 大字时钟 hh:mm 居中 */
    char timeStr[16];
    sprintf_s(timeStr, "%02d:%02d", m_hour, m_min);
    int tw = GetStrPixelWidth(timeStr);
    DrawStrWrap(m_hMemDC, (OLED_W - tw) / 2, 8, timeStr, PX_COLOR, OLED_W);

    /* 日期+星期 自动换行 (紧凑格式, 避免溢出) */
    char dateStr[64];
    int n1 = sprintf_s(dateStr, sizeof(dateStr), "%02d", m_year % 100);
    strcat_s(dateStr, sizeof(dateStr), "年");
    n1 = (int)strlen(dateStr);
    n1 += sprintf_s(dateStr + n1, sizeof(dateStr) - n1, "%02d", m_month);
    strcat_s(dateStr, sizeof(dateStr), "月");
    n1 = (int)strlen(dateStr);
    n1 += sprintf_s(dateStr + n1, sizeof(dateStr) - n1, "%02d", m_day);
    strcat_s(dateStr, sizeof(dateStr), "\xe6\x97\xa5 ");
    strcat_s(dateStr, sizeof(dateStr), "星期");
    strcat_s(dateStr, sizeof(dateStr), week_names[m_wday]);
    DrawStrWrap(m_hMemDC, 0, 32, dateStr, PX_COLOR, OLED_W);

    /* 同步帧缓冲 */
    for (int y = 0; y < OLED_H; y++)
        for (int x = 0; x < OLED_W; x++)
            if (GetPixel(m_hMemDC, x, y) != BG_COLOR)
                SetPixelToBuf(x, y, true);
}

/* ---- 远程子模式: WEATHER ---- */
void OledPreview::RenderWeatherMode() {
    RECT rc = {0, 0, OLED_W, OLED_H};
    HBRUSH bg = CreateSolidBrush(BG_COLOR);
    FillRect(m_hMemDC, &rc, bg);
    DeleteObject(bg);

    /* 天气图标 + 名称 (第 1 行) */
    const uint8_t* icon = weather_icons_8x8[m_weatherType % 7];
    DrawChar(m_hMemDC, 8, 8, icon, 8, 8, PX_COLOR);
    DrawStrWrap(m_hMemDC, 24, 4, weather_names[m_weatherType % 7], PX_COLOR, OLED_W);

    /* 温度+湿度+风向 (自动换行) */
    char buf[96];
    const char* wind_names[] = {"北","东北","东","东南",
                                "南","西南","西","西北"};
    buf[0] = '\0';
    strcat_s(buf, sizeof(buf), "\xe6\xb8\xa9\xe5\xba\xa6:");
    _itoa_s(m_temp, buf + strlen(buf), sizeof(buf) - strlen(buf), 10);
    strcat_s(buf, sizeof(buf), "C ");
    strcat_s(buf, sizeof(buf), "\xe6\xb9\xbf\xe5\xba\xa6:");
    _itoa_s(m_humidity, buf + strlen(buf), sizeof(buf) - strlen(buf), 10);
    strcat_s(buf, sizeof(buf), "%% ");
    strcat_s(buf, sizeof(buf), "\xe9\xa3\x8e\xe5\x90\x91:");
    strcat_s(buf, sizeof(buf), wind_names[m_wind % 8]);
    DrawStrWrap(m_hMemDC, 0, 20, buf, PX_COLOR, OLED_W);

    /* 同步帧缓冲 */
    for (int y = 0; y < OLED_H; y++)
        for (int x = 0; x < OLED_W; x++)
            if (GetPixel(m_hMemDC, x, y) != BG_COLOR)
                SetPixelToBuf(x, y, true);
}

/* ---- 远程子模式: DATE ---- */
void OledPreview::RenderDateMode() {
    RECT rc = {0, 0, OLED_W, OLED_H};
    HBRUSH bg = CreateSolidBrush(BG_COLOR);
    FillRect(m_hMemDC, &rc, bg);
    DeleteObject(bg);

    /* 日期+星期 (自动换行) */
    char dateStr[64];
    int n2 = sprintf_s(dateStr, sizeof(dateStr), "%02d", m_year % 100);
    strcat_s(dateStr, sizeof(dateStr), "年");
    n2 = (int)strlen(dateStr);
    n2 += sprintf_s(dateStr + n2, sizeof(dateStr) - n2, "%02d", m_month);
    strcat_s(dateStr, sizeof(dateStr), "月");
    n2 = (int)strlen(dateStr);
    n2 += sprintf_s(dateStr + n2, sizeof(dateStr) - n2, "%02d", m_day);
    strcat_s(dateStr, sizeof(dateStr), "\xe6\x97\xa5 ");
    strcat_s(dateStr, sizeof(dateStr), "星期");
    strcat_s(dateStr, sizeof(dateStr), week_names[m_wday]);
    int dw = GetStrPixelWidth(dateStr);
    DrawStrWrap(m_hMemDC, (OLED_W - dw) / 2, 16, dateStr, PX_COLOR, OLED_W);

    /* 同步帧缓冲 */
    for (int y = 0; y < OLED_H; y++)
        for (int x = 0; x < OLED_W; x++)
            if (GetPixel(m_hMemDC, x, y) != BG_COLOR)
                SetPixelToBuf(x, y, true);
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
void OledPreview::SetRemote(bool r) { m_isRemote = r; Render(); }
void OledPreview::SetSubMode(int sm) { if (sm >= 0 && sm <= 3) { m_subMode = sm; Render(); } }

/* ---- Animation tick ---- */
void OledPreview::Tick() {
    DWORD now = GetTickCount();

    /* 本地模式 + 远程 TEXT 子模式均支持特效 */
    if (m_isRemote && m_subMode != 0) return;

    int tw = GetStrPixelWidth(m_text.c_str());

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
            if (m_scrollY > 64) m_scrollY = 0;
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
