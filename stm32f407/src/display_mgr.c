/**
 * @file    display_mgr.c
 * @brief   显示管理器实现 — 本地/远程双模式 (v3.1)
 *
 * 本地模式: STM32 全屏渲染文字+7种特效 (128×64, 无状态栏)
 * 远程模式: 接收PC帧缓冲分段→拼装→刷屏 (STM32不自行渲染)
 */

#include "display_mgr.h"
#include "ssd1306.h"
#include "font.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* --- 内部常量 --- */
#define CONTENT_MAX_LEN     256
#define BOOT_TEXT_MAX_LEN   128
#define SCROLL_STEP_PX      2
#define SCROLL_INTERVAL_MS  40
#define FLIP_INTERVAL_MS    3000
#define FADE_INTERVAL_MS    30
#define FADE_STEPS          32

#define FRAME_BUF_SIZE      (SSD1306_WIDTH * SSD1306_PAGES)  /* 1024 */

/* --- 静态变量 --- */

/* 模式管理 */
static bool            is_remote = false;
static uint8_t         remote_sub_mode = REMOTE_SUB_TEXT;

/* 本地模式 */
static display_mode_t  current_mode = DISP_MODE_STATIC;
static char            content_text[CONTENT_MAX_LEN] = {0};
static char            boot_text[BOOT_TEXT_MAX_LEN] = "欢迎进入系统";
static int16_t         scroll_offset_x = 0;
static uint8_t         flip_phase = 0;
static uint32_t        flip_timer = 0;
static uint8_t         fade_step = 0;
static uint8_t         fade_dir = 0;
static uint32_t        fade_timer = 0;

/* 时间/天气数据 (仅存储) */
static display_status_t status = {0};

/* 远程模式帧缓冲 */
static uint8_t  frame_buf[FRAME_BUF_SIZE] = {0};
static uint8_t  frame_seg_received = 0;
static uint8_t  frame_seg_total = 0;

/* 远程模式串口断开提示状态 */
static bool     disconnect_msg_shown = false;

/* 菜单激活时抑制 display_mgr 内部刷屏 */
static bool     menu_suppress = false;

/* 特效函数指针表 */
typedef void (*effect_func_t)(void);
static effect_func_t effects[DISP_MODE_COUNT];

static void effect_static(void);
static void effect_scroll_left(void);
static void effect_scroll_right(void);
static void effect_scroll_up(void);
static void effect_scroll_down(void);
static void effect_flip(void);
static void effect_fade(void);

/* --- 内部辅助 --- */

/* 清空全屏 buffer (128×64 = 1024 字节) */
static void clear_full_screen(void)
{
    uint8_t *buf = ssd1306_get_buffer();
    memset(buf, 0x00, FRAME_BUF_SIZE);
}

/* 仅在 menu_suppress 为 false 时刷屏 */
static void update_screen_if_allowed(void)
{
    if (!menu_suppress) {
        ssd1306_update_screen();
    }
}



static void draw_chinese_char(uint8_t x, uint8_t page, const uint8_t *glyph)
{
    if (!glyph) return;
    uint8_t *buf = ssd1306_get_buffer();
    for (uint8_t c = 0; c < FONT_CHINESE_W && x + c < SSD1306_WIDTH; c++, x++) {
        if (page     < SSD1306_PAGES) buf[page     * SSD1306_WIDTH + x] = glyph[c * 2];
        if (page + 1 < SSD1306_PAGES) buf[(page + 1) * SSD1306_WIDTH + x] = glyph[c * 2 + 1];
    }
}

/*
 * 全屏绘制 UTF-8 文字 (从 page 0 开始, 128×64 全屏)
 * 中文字符 16×16, ASCII 字符 8×16, 自动换行
 */
static void draw_text_fullscreen(const char *text)
{
    clear_full_screen();
    uint8_t page = 0;
    uint8_t col = 0;
    const char *p = text;
    uint8_t *buf = ssd1306_get_buffer();

    while (*p && page < SSD1306_PAGES - 1) {
        uint8_t first = (uint8_t)*p;

        if ((first & 0x80) && (first & 0xF0) == 0xE0) {
            /* 3 字节 UTF-8 中文 */
            const uint8_t *glyph = font_get_chinese_utf8(p);
            if (col + FONT_CHINESE_W > SSD1306_WIDTH) {
                col = 0; page += 2;
                if (page >= SSD1306_PAGES - 1) break;
            }
            if (glyph) {
                draw_chinese_char(col, page, glyph);
            } else {
                /* 缺字显示实心方块 */
                for (uint8_t c = 0; c < FONT_CHINESE_W && col + c < SSD1306_WIDTH; c++) {
                    if (page     < SSD1306_PAGES) buf[page     * SSD1306_WIDTH + col + c] = 0xFF;
                    if (page + 1 < SSD1306_PAGES) buf[(page + 1) * SSD1306_WIDTH + col + c] = 0xFF;
                }
            }
            col += FONT_CHINESE_W;
            p += 3;
        } else {
            /* ASCII */
            if (col + FONT_ASCII_W > SSD1306_WIDTH) {
                col = 0; page += 2;
                if (page >= SSD1306_PAGES - 1) break;
            }
            const uint8_t *bm = font_get_ascii(*p);
            for (uint8_t c = 0; c < FONT_ASCII_W && col < SSD1306_WIDTH; c++, col++) {
                if (page     < SSD1306_PAGES) buf[page     * SSD1306_WIDTH + col] = bm[c];
                if (page + 1 < SSD1306_PAGES) buf[(page + 1) * SSD1306_WIDTH + col] = bm[c + 8];
            }
            p++;
        }
    }
}

/*
 * 计算 text 在 SSD1306 上的像素宽度。
 * 中文字符=16px, ASCII字符=8px。
 */
static int16_t calc_text_pixel_width(const char *text)
{
    int16_t w = 0;
    const char *p = text;
    while (*p) {
        uint8_t first = (uint8_t)*p;
        if ((first & 0x80) && (first & 0xF0) == 0xE0) {
            w += FONT_CHINESE_W;
            p += 3;
        } else if ((first & 0x80) && (first & 0xE0) == 0xC0) {
            p += 2;
        } else {
            w += FONT_ASCII_W;
            p++;
        }
    }
    return w;
}

/* --- 特效实现 (全屏 128×64) --- */

static void effect_static(void)
{
    draw_text_fullscreen(content_text);
}

static void effect_scroll_left(void)
{
    uint8_t *buf = ssd1306_get_buffer();
    for (uint8_t p = 0; p < SSD1306_PAGES; p++) {
        uint8_t *line = &buf[p * SSD1306_WIDTH];
        memmove(line, line + SCROLL_STEP_PX, SSD1306_WIDTH - SCROLL_STEP_PX);
        memset(line + SSD1306_WIDTH - SCROLL_STEP_PX, 0, SCROLL_STEP_PX);
    }
    scroll_offset_x += SCROLL_STEP_PX;
    if (scroll_offset_x >= calc_text_pixel_width(content_text)) {
        scroll_offset_x = 0;
    }
}

static void effect_scroll_right(void)
{
    uint8_t *buf = ssd1306_get_buffer();
    for (uint8_t p = 0; p < SSD1306_PAGES; p++) {
        uint8_t *line = &buf[p * SSD1306_WIDTH];
        memmove(line + SCROLL_STEP_PX, line, SSD1306_WIDTH - SCROLL_STEP_PX);
        memset(line, 0, SCROLL_STEP_PX);
    }
}

static void effect_scroll_up(void)
{
    uint8_t *buf = ssd1306_get_buffer();
    for (uint8_t p = 0; p < SSD1306_PAGES - 1; p++) {
        memcpy(&buf[p * SSD1306_WIDTH],
               &buf[(p + 1) * SSD1306_WIDTH],
               SSD1306_WIDTH);
    }
    memset(&buf[(SSD1306_PAGES - 1) * SSD1306_WIDTH], 0, SSD1306_WIDTH);
}

static void effect_scroll_down(void)
{
    uint8_t *buf = ssd1306_get_buffer();
    for (uint8_t p = SSD1306_PAGES - 1; p > 0; p--) {
        memcpy(&buf[p * SSD1306_WIDTH],
               &buf[(p - 1) * SSD1306_WIDTH],
               SSD1306_WIDTH);
    }
    memset(&buf[0], 0, SSD1306_WIDTH);
}

static void effect_flip(void)  { /* tick 中处理 */ }
static void effect_fade(void)  { /* tick 中处理 */ }

/* --- 公开接口: 初始化 --- */

void display_mgr_init(const char *boot_text_str)
{
    effects[DISP_MODE_STATIC]   = effect_static;
    effects[DISP_MODE_SCROLL_L] = effect_scroll_left;
    effects[DISP_MODE_SCROLL_R] = effect_scroll_right;
    effects[DISP_MODE_SCROLL_U] = effect_scroll_up;
    effects[DISP_MODE_SCROLL_D] = effect_scroll_down;
    effects[DISP_MODE_FLIP]     = effect_flip;
    effects[DISP_MODE_FADE]     = effect_fade;

    if (boot_text_str && boot_text_str[0]) {
        strncpy(boot_text, boot_text_str, BOOT_TEXT_MAX_LEN - 1);
        boot_text[BOOT_TEXT_MAX_LEN - 1] = '\0';
    }
    strncpy(content_text, boot_text, CONTENT_MAX_LEN - 1);
    content_text[CONTENT_MAX_LEN - 1] = '\0';

    is_remote = false;
    remote_sub_mode = REMOTE_SUB_TEXT;
    current_mode = DISP_MODE_STATIC;
    scroll_offset_x = 0;
    flip_phase = 0;
    fade_step = 0;
    fade_dir = 0;

    memset(&status, 0, sizeof(status));
    memset(frame_buf, 0, FRAME_BUF_SIZE);
    frame_seg_received = 0;
    frame_seg_total = 0;

    draw_text_fullscreen(content_text);
    ssd1306_update_screen();
}

/* --- 公开接口: 本地/远程模式切换 --- */

void display_mgr_set_remote(bool remote)
{
    if (remote == is_remote) return;

    is_remote = remote;

    if (remote) {
        /* 切换到远程模式: 清屏准备接收帧缓冲 */
        clear_full_screen();
        update_screen_if_allowed();
        memset(frame_buf, 0, FRAME_BUF_SIZE);
        frame_seg_received = 0;
        frame_seg_total = 0;
    } else {
        /* 切换到本地模式: 恢复本地渲染 */
        ssd1306_set_contrast(255);
        scroll_offset_x = 0;
        flip_phase = 0;
        fade_step = 0;
        fade_dir = 0;
        draw_text_fullscreen(content_text);
        update_screen_if_allowed();
    }
}

bool display_mgr_is_remote(void)
{
    return is_remote;
}

/* --- 公开接口: 远程子模式 --- */

void display_mgr_set_sub_mode(uint8_t sub_mode)
{
    remote_sub_mode = sub_mode;
}

uint8_t display_mgr_get_sub_mode(void)
{
    if (!is_remote) return REMOTE_SUB_TEXT;
    return remote_sub_mode;
}

/* --- 公开接口: 远程帧缓冲接收 --- */

void display_mgr_rx_frame_seg(uint8_t seg, uint8_t total, const uint8_t *data, uint8_t len)
{
    if (!is_remote) return;
    if (total == 0) return;

    uint16_t offset = (uint16_t)seg * 200;
    uint16_t copy_len = (offset + len > FRAME_BUF_SIZE) ? (FRAME_BUF_SIZE - offset) : len;

    if (seg == 0) {
        memset(frame_buf, 0, FRAME_BUF_SIZE);
        frame_seg_received = 0;
        frame_seg_total = total;
    }

    if (seg >= total) return;

    memcpy(&frame_buf[offset], data, copy_len);
    frame_seg_received++;

    if (frame_seg_received >= frame_seg_total) {
        uint8_t *buf = ssd1306_get_buffer();
        memcpy(buf, frame_buf, FRAME_BUF_SIZE);
        /* 菜单激活期间只拼装帧缓冲, 不直接刷屏 */
        if (!menu_suppress) {
            ssd1306_update_screen();
        }
        frame_seg_received = 0;
    }
}

/* --- 公开接口: 本地模式 文字+特效 --- */

void display_mgr_set_text(const char *text)
{
    if (text) {
        strncpy(content_text, text, CONTENT_MAX_LEN - 1);
        content_text[CONTENT_MAX_LEN - 1] = '\0';
        scroll_offset_x = 0;
        if (!is_remote) {
            draw_text_fullscreen(content_text);
            update_screen_if_allowed();
        }
    }
}

void display_mgr_set_boot_text(const char *text)
{
    if (text) {
        strncpy(boot_text, text, BOOT_TEXT_MAX_LEN - 1);
        boot_text[BOOT_TEXT_MAX_LEN - 1] = '\0';
    }
}

const char* display_mgr_get_boot_text(void)
{
    return boot_text;
}

void display_mgr_set_mode(display_mode_t mode)
{
    if (mode >= DISP_MODE_COUNT) return;
    ssd1306_scroll_stop();
    current_mode = mode;
    scroll_offset_x = 0;
    fade_step = 0;

    if (mode == DISP_MODE_STATIC) {
        draw_text_fullscreen(content_text);
    } else if (mode == DISP_MODE_FADE) {
        fade_dir = 1;
        fade_step = 0;
        ssd1306_set_contrast(0);
        draw_text_fullscreen(content_text);
    } else {
        draw_text_fullscreen(content_text);
    }

    if (!is_remote) {
        update_screen_if_allowed();
    }
}

display_mode_t display_mgr_get_mode(void)
{
    return current_mode;
}

void display_mgr_next_mode(void)
{
    display_mode_t next = (display_mode_t)((current_mode + 1) % DISP_MODE_COUNT);
    display_mgr_set_mode(next);
}

/* --- 公开接口: 时间/天气数据 (仅存储, v3.1不绘制状态栏) --- */

void display_mgr_update_status(const display_status_t *new_status)
{
    if (new_status) {
        memcpy(&status, new_status, sizeof(display_status_t));
    }
}

const display_status_t* display_mgr_get_status(void)
{
    return &status;
}

/* --- 公开接口: 远程模式串口断开提示 --- */

void display_mgr_show_disconnect(void)
{
    if (!is_remote || disconnect_msg_shown) return;

    disconnect_msg_shown = true;
    clear_full_screen();
    /* 在 OLED 中央显示 "串口已断开" */
    const char *msg = "串口已断开"; /* UTF-8: 串口已断开 */
    uint8_t *buf = ssd1306_get_buffer();
    memset(buf, 0, FRAME_BUF_SIZE);

    /* 估算 5 个中文字符 ≈ 80px, 居中 x=24, page=2 */
    const char *p = msg;
    uint8_t col = 24;
    uint8_t page = 2;
    while (*p && page < SSD1306_PAGES - 1) {
        const uint8_t *glyph = font_get_chinese_utf8(p);
        if (glyph) draw_chinese_char(col, page, glyph);
        col += FONT_CHINESE_W;
        p += 3;
    }
    ssd1306_update_screen();
}

void display_mgr_hide_disconnect(void)
{
    disconnect_msg_shown = false;
}

/* --- 公开接口: 双模式主 tick --- */

void display_mgr_tick(void)
{
    if (is_remote) {
        /* 远程模式: 帧缓冲由 rx_frame_seg 驱动, tick 不做额外操作 */
        return;
    }

    /* --- 本地模式: 特效 tick --- */

    switch (current_mode) {
    case DISP_MODE_STATIC:
        break;

    case DISP_MODE_SCROLL_L:
    case DISP_MODE_SCROLL_R:
    case DISP_MODE_SCROLL_U:
    case DISP_MODE_SCROLL_D:
        if (effects[current_mode]) {
            effects[current_mode]();
            ssd1306_update_screen();
        }
        break;

    case DISP_MODE_FLIP:
        flip_timer += 50;
        if (flip_timer >= FLIP_INTERVAL_MS) {
            flip_timer = 0;
            flip_phase ^= 1;
            draw_text_fullscreen(flip_phase ? content_text : "");
            ssd1306_update_screen();
        }
        break;

    case DISP_MODE_FADE:
        fade_timer += 50;
        if (fade_timer >= FADE_INTERVAL_MS) {
            fade_timer = 0;
            if (fade_dir == 1) {
                fade_step++;
                if (fade_step >= FADE_STEPS) fade_dir = 0;
            } else {
                fade_step--;
                if (fade_step == 0) fade_dir = 1;
            }
            uint8_t contrast = (uint8_t)((uint32_t)fade_step * 255 / FADE_STEPS);
            ssd1306_set_contrast(contrast);
        }
        break;

    case DISP_MODE_COUNT:
    default:
        break;
    }
}

/* --- 公开接口: 菜单激活期间抑制远程帧刷屏 --- */

void display_mgr_set_menu_suppress(bool suppress)
{
    menu_suppress = suppress;
}

/* --- 公开接口: 菜单退出后恢复显示 --- */

void display_mgr_redraw(void)
{
    menu_suppress = false;

    if (is_remote) {
        /* 远程模式: 将最近拼装完成的帧刷到屏幕 */
        uint8_t *buf = ssd1306_get_buffer();
        memcpy(buf, frame_buf, FRAME_BUF_SIZE);
        ssd1306_update_screen();
    } else {
        /* 本地模式: 重绘当前文字 + 特效 */
        ssd1306_set_contrast(255);
        scroll_offset_x = 0;
        flip_phase = 0;
        fade_step = 0;
        fade_dir = 0;
        draw_text_fullscreen(content_text);
        ssd1306_update_screen();
    }
}

