/**
 * @file    display_mgr.c
 * @brief   显示管理器实现
 *
 * 区域:
 *   [0, 0] ~ [127, 15]  顶部状态栏 (时间/天气/LED)
 *   [0,16] ~ [127, 63] 主内容区 (用户文字 + 特效)
 */

#include "display_mgr.h"
#include "ssd1306.h"
#include "font.h"
#include "protocol.h"
#include "uart_drv.h"
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

#define FRAME_SYNC_INTERVAL_MS  200
#define FRAME_SYNC_PAYLOAD      200

/* --- 静态变量 --- */
static display_mode_t  current_mode = DISP_MODE_STATIC;
static display_status_t status = {0};
static char content_text[CONTENT_MAX_LEN] = {0};
static char boot_text[BOOT_TEXT_MAX_LEN] = "欢迎进入系统";

static int16_t scroll_offset_x = 0;
static int16_t content_pixel_width = 0;

static uint8_t flip_phase = 0;
static uint32_t flip_timer = 0;

static uint8_t fade_step = 0;
static uint8_t fade_dir = 0;
static uint32_t fade_timer = 0;

static uint32_t frame_sync_timer = 0;
static uint8_t  frame_sync_seg = 0;

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

/*
 * 绘制 16px 高 ASCII 字符, 跨两个 page。
 * y 必须对齐到 8px (page) 边界, 否则内容错位。
 */
static void draw_str_16h(uint8_t x, uint8_t y, const char *str)
{
    assert((y & 0x07) == 0);  /* y 必须为 page 起始位置 */
    uint8_t page = y >> 3;
    uint8_t *buf = ssd1306_get_buffer();

    while (*str && x < SSD1306_WIDTH) {
        const uint8_t *bm = font_get_ascii(*str);
        for (uint8_t c = 0; c < FONT_ASCII_W && x < SSD1306_WIDTH; c++, x++) {
            if (page < SSD1306_PAGES)
                buf[page * SSD1306_WIDTH + x] = bm[c];
            if (page + 1 < SSD1306_PAGES)
                buf[(page + 1) * SSD1306_WIDTH + x] = bm[c + 8];
        }
        str++;
    }
}

static void refresh_content_area(uint8_t fill_bg)
{
    uint8_t *buf = ssd1306_get_buffer();
    uint8_t start_page = DISP_CONTENT_Y >> 3;
    if (fill_bg) {
        for (uint8_t p = start_page; p < SSD1306_PAGES; p++) {
            memset(&buf[p * SSD1306_WIDTH], 0x00, SSD1306_WIDTH);
        }
    }
}

static void draw_chinese_char(uint8_t x, uint8_t page, const uint8_t *glyph)
{
    if (!glyph) return;
    uint8_t *buf = ssd1306_get_buffer();
    for (uint8_t c = 0; c < FONT_CHINESE_W && x + c < SSD1306_WIDTH; c++, x++) {
        if (page < SSD1306_PAGES)
            buf[page * SSD1306_WIDTH + x] = glyph[c * 2];
        if (page + 1 < SSD1306_PAGES)
            buf[(page + 1) * SSD1306_WIDTH + x] = glyph[c * 2 + 1];
    }
}

static void draw_text_content(const char *text)
{
    refresh_content_area(1);
    uint8_t page = DISP_CONTENT_Y >> 3;
    uint8_t col = 0;
    const char *p = text;
    uint8_t *buf = ssd1306_get_buffer();

    while (*p && page < SSD1306_PAGES - 1) {
        uint8_t first = (uint8_t)*p;

        if ((first & 0x80) && (first & 0xF0) == 0xE0) {
            const uint8_t *glyph = font_get_chinese_utf8(p);
            if (col + FONT_CHINESE_W > SSD1306_WIDTH) {
                col = 0; page += 2;
                if (page >= SSD1306_PAGES - 1) break;
            }
            if (glyph) {
                draw_chinese_char(col, page, glyph);
            } else {
                for (uint8_t c = 0; c < FONT_CHINESE_W && col + c < SSD1306_WIDTH; c++) {
                    if (page < SSD1306_PAGES)
                        buf[page * SSD1306_WIDTH + col + c] = 0xFF;
                    if (page + 1 < SSD1306_PAGES)
                        buf[(page + 1) * SSD1306_WIDTH + col + c] = 0xFF;
                }
            }
            col += FONT_CHINESE_W;
            p += 3;
        } else {
            if (col + FONT_ASCII_W > SSD1306_WIDTH) {
                col = 0; page += 2;
                if (page >= SSD1306_PAGES - 1) break;
            }
            const uint8_t *bm = font_get_ascii(*p);
            for (uint8_t c = 0; c < FONT_ASCII_W && col < SSD1306_WIDTH; c++, col++) {
                if (page < SSD1306_PAGES)
                    buf[page * SSD1306_WIDTH + col] = bm[c];
                if (page + 1 < SSD1306_PAGES)
                    buf[(page + 1) * SSD1306_WIDTH + col] = bm[c + 8];
            }
            p++;
        }
    }
}

/* --- 特效实现 --- */

static void effect_static(void)
{
    draw_text_content(content_text);
}

static void effect_scroll_left(void)
{
    uint8_t *buf = ssd1306_get_buffer();
    uint8_t sp = DISP_CONTENT_Y >> 3;   /* page 2 */
    uint8_t ep = SSD1306_PAGES - 1;     /* page 7 */
    assert(sp <= ep && ep < SSD1306_PAGES);

    for (uint8_t p = sp; p <= ep; p++) {
        uint8_t *line = &buf[p * SSD1306_WIDTH];
        memmove(line, line + SCROLL_STEP_PX, SSD1306_WIDTH - SCROLL_STEP_PX);
        memset(line + SSD1306_WIDTH - SCROLL_STEP_PX, 0, SCROLL_STEP_PX);
    }
    scroll_offset_x += SCROLL_STEP_PX;
    if (scroll_offset_x >= content_pixel_width) {
        scroll_offset_x = 0;
    }
}

static void effect_scroll_right(void)
{
    uint8_t *buf = ssd1306_get_buffer();
    uint8_t sp = DISP_CONTENT_Y >> 3;
    uint8_t ep = SSD1306_PAGES - 1;
    assert(sp <= ep && ep < SSD1306_PAGES);

    for (uint8_t p = sp; p <= ep; p++) {
        uint8_t *line = &buf[p * SSD1306_WIDTH];
        memmove(line + SCROLL_STEP_PX, line, SSD1306_WIDTH - SCROLL_STEP_PX);
        memset(line, 0, SCROLL_STEP_PX);
    }
}

static void effect_scroll_up(void)
{
    uint8_t *buf = ssd1306_get_buffer();
    uint8_t sp = DISP_CONTENT_Y >> 3;   /* page 2 */
    uint8_t ep = SSD1306_PAGES - 1;     /* page 7 */
    assert(sp <= ep && ep < SSD1306_PAGES);

    /*
     * 注意: 只搬移内容区 (page sp~ep), 不污染状态栏 (page 0~1)
     */
    for (uint8_t p = sp; p < ep; p++) {
        memcpy(&buf[p * SSD1306_WIDTH],
               &buf[(p + 1) * SSD1306_WIDTH],
               SSD1306_WIDTH);
    }
    memset(&buf[ep * SSD1306_WIDTH], 0, SSD1306_WIDTH);
}

static void effect_scroll_down(void)
{
    uint8_t *buf = ssd1306_get_buffer();
    uint8_t sp = DISP_CONTENT_Y >> 3;
    uint8_t ep = SSD1306_PAGES - 1;
    assert(sp <= ep && ep < SSD1306_PAGES);

    for (uint8_t p = ep; p > sp; p--) {
        memcpy(&buf[p * SSD1306_WIDTH],
               &buf[(p - 1) * SSD1306_WIDTH],
               SSD1306_WIDTH);
    }
    memset(&buf[sp * SSD1306_WIDTH], 0, SSD1306_WIDTH);
}

static void effect_flip(void) { /* tick 中处理 */ }
static void effect_fade(void) { /* tick 中处理 */ }

/* --- 状态栏绘制 --- */

static const char* weather_labels[] = {
    "Sun", "Cld", "Ovc", "LtR", "HvR", "Trm", "Sno"
};

static void draw_status_bar(void)
{
    char line[32];
    uint8_t *buf = ssd1306_get_buffer();
    memset(&buf[0], 0, SSD1306_WIDTH * 2);  /* 清空 page 0,1 */

    snprintf(line, sizeof(line), "%02d:%02d:%02d",
             status.hour, status.minute, status.second);
    draw_str_16h(0, 0, line);

    if (status.weather_type < 7) {
        draw_str_16h(72, 0, weather_labels[status.weather_type]);
    }

    snprintf(line, sizeof(line), "%dC", (int)status.temperature);
    draw_str_16h(100, 0, line);
}

/* --- 公开接口 --- */

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
    }
    strncpy(content_text, boot_text, CONTENT_MAX_LEN - 1);

    current_mode = DISP_MODE_STATIC;

    refresh_content_area(1);
    draw_text_content(content_text);

    status.hour = 0; status.minute = 0; status.second = 0;
    status.weather_type = 0;
    display_mgr_update_status(&status);

    ssd1306_update_screen();
}

void display_mgr_set_text(const char *text)
{
    if (text) {
        strncpy(content_text, text, CONTENT_MAX_LEN - 1);
        content_text[CONTENT_MAX_LEN - 1] = '\0';
        scroll_offset_x = 0;
        draw_text_content(content_text);
        ssd1306_update_screen();
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
    refresh_content_area(1);
    current_mode = mode;
    scroll_offset_x = 0;
    fade_step = 0;

    if (mode == DISP_MODE_STATIC) {
        draw_text_content(content_text);
    } else if (mode == DISP_MODE_FADE) {
        fade_dir = 1;
        fade_step = 0;
        ssd1306_set_contrast(0);
        draw_text_content(content_text);
    }
    ssd1306_update_screen();
}

display_mode_t display_mgr_get_mode(void) { return current_mode; }

void display_mgr_next_mode(void)
{
    display_mode_t next = (display_mode_t)((current_mode + 1) % DISP_MODE_COUNT);
    display_mgr_set_mode(next);
}

void display_mgr_update_status(const display_status_t *new_status)
{
    if (new_status) {
        memcpy(&status, new_status, sizeof(display_status_t));
    }
    draw_status_bar();
    ssd1306_update_screen();
}

const display_status_t* display_mgr_get_status(void) { return &status; }

void display_mgr_tick(void)
{
    static uint32_t tick_count = 0;
    tick_count++;

    frame_sync_timer += 50;
    if (frame_sync_timer >= FRAME_SYNC_INTERVAL_MS) {
        frame_sync_timer = 0;
        display_mgr_sync_frame();
    }

    switch (current_mode) {
    case DISP_MODE_SCROLL_L:
    case DISP_MODE_SCROLL_R:
    case DISP_MODE_SCROLL_U:
    case DISP_MODE_SCROLL_D:
        if (effects[current_mode]) {
            effects[current_mode]();
            draw_status_bar();
            ssd1306_update_screen();
        }
        break;

    case DISP_MODE_FLIP:
        flip_timer += 50;
        if (flip_timer >= FLIP_INTERVAL_MS) {
            flip_timer = 0;
            flip_phase ^= 1;
            refresh_content_area(1);
            if (flip_phase == 0) {
                draw_text_content(content_text);
            } else {
                char tmp[CONTENT_MAX_LEN];
                snprintf(tmp, sizeof(tmp), "... %s ...", content_text);
                draw_text_content(tmp);
            }
            draw_status_bar();
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

    default:
        break;
    }
}

void display_mgr_sync_frame(void)
{
    const uint8_t *frame_buf = ssd1306_get_buffer();
    uint16_t total = SSD1306_WIDTH * SSD1306_PAGES;
    uint8_t seg_total = (total + FRAME_SYNC_PAYLOAD - 1) / FRAME_SYNC_PAYLOAD;

    if (frame_sync_seg >= seg_total) frame_sync_seg = 0;

    uint16_t offset = frame_sync_seg * FRAME_SYNC_PAYLOAD;
    uint16_t remain = total - offset;
    uint8_t payload_len = (remain > FRAME_SYNC_PAYLOAD) ? FRAME_SYNC_PAYLOAD : (uint8_t)remain;

    /*
     * 构建帧到局部缓冲区 (不入 proto 全局 tx_buf),
     * 避免与 process_frame 中的 ACK/NAK 发送竞争。
     */
    uint8_t seg_buf[256];  /* 局部发送缓冲区 */
    uint8_t idx = 0;
    seg_buf[idx++] = 0xA5;           /* SOF */
    seg_buf[idx++] = payload_len + 2; /* LEN (含 seg+total) */
    seg_buf[idx++] = CMD_FRAME_SYNC; /* CMD */
    seg_buf[idx++] = frame_sync_seg; /* SEQ */
    seg_buf[idx++] = frame_sync_seg; /* seg */
    seg_buf[idx++] = seg_total;      /* total */
    memcpy(&seg_buf[idx], &frame_buf[offset], payload_len);
    idx += payload_len;

    /* CRC 覆盖 SOF..DATA */
    uint8_t crc = proto_crc8(seg_buf, idx);
    seg_buf[idx++] = crc;
    seg_buf[idx++] = 0x5A;  /* EOF */

    uart_drv_send(seg_buf, idx);

    frame_sync_seg++;
}
