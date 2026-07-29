/**
 * @file    display_mgr.c
 * @brief   显示管理器实现
 *
 * 区域:
 *   [0, 0] ~ [127, 15]  顶部状态栏 (时间/天气/LED)
 *   [0,16] ~ [127, 63] 主内容区 (用户文字 + 特效)
 *
 * 特效实现:
 *   - 滚动: 软件实现, 在 buffer 中逐像素/逐行搬移
 *   - 翻页: 每 N 秒切换显示的文字段落
 *   - 淡入淡出: 通过调整对比度模拟
 */

#include "display_mgr.h"
#include "ssd1306.h"
#include "font.h"
#include "protocol.h"
#include "uart_drv.h"
#include <string.h>
#include <stdio.h>

/* --- 内部常量 --- */
#define CONTENT_MAX_LEN     256     /* 最大内容文字长度 */
#define BOOT_TEXT_MAX_LEN   128
#define SCROLL_STEP_PX      2       /* 每次滚动像素数 */
#define SCROLL_INTERVAL_MS  40      /* 滚动 tick 间隔 */
#define FLIP_INTERVAL_MS    3000    /* 翻页间隔 */
#define FADE_INTERVAL_MS    30      /* 淡入淡出间隔 */
#define FADE_STEPS          32      /* 淡入淡出步数 */

#define FRAME_SYNC_INTERVAL_MS  200 /* 显存同步上报间隔 */
#define FRAME_SYNC_PAYLOAD      200 /* 每帧最大 payload 字节数 */

/* --- 静态变量 --- */
static display_mode_t  current_mode = DISP_MODE_STATIC;
static display_status_t status = {0};
static char content_text[CONTENT_MAX_LEN] = {0};
static char boot_text[BOOT_TEXT_MAX_LEN] = "欢迎进入系统";

/* 滚动状态 */
static int16_t scroll_offset_x = 0;   /* 水平滚动偏移 */
static int16_t scroll_offset_y = 0;   /* 垂直滚动偏移 */
static int16_t content_pixel_width = 0;  /* 内容像素总宽度 */
static uint8_t scroll_dirty = 0;

/* 翻页状态 */
static uint8_t flip_phase = 0;       /* 0=显示当前, 1=过渡到下一页 */
static uint32_t flip_timer = 0;

/* 淡入淡出状态 */
static uint8_t fade_step = 0;
static uint8_t fade_dir = 0;         /* 0=淡出, 1=淡入 */
static uint32_t fade_timer = 0;

/* 显存同步状态 */
static uint32_t frame_sync_timer = 0;
static uint8_t  frame_sync_seg = 0;

/* 特效函数指针表 */
typedef void (*effect_func_t)(void);
static effect_func_t effects[DISP_MODE_COUNT];

/* 前向声明 */
static void effect_static(void);
static void effect_scroll_left(void);
static void effect_scroll_right(void);
static void effect_scroll_up(void);
static void effect_scroll_down(void);
static void effect_flip(void);
static void effect_fade(void);

/* --- 内部辅助 --- */

/*
 * 注意: ASCII 字体为 8×16 (跨两个 page),
 * 下方 draw_str_16h() 正确处理了双 page 写入。
 * draw_char/draw_string/draw_line_pg 仅写入单 page,
 * 仅供 page 内单行使用, 当前未被调用, 保留作为单行工具。
 */

/* 在指定位置绘制 ASCII 字符 (仅写入指定 page, 用于 8px 高单行) */
static void draw_char_pg(uint8_t x, uint8_t page, char ch)
{
    if (x + 7 >= SSD1306_WIDTH || page >= SSD1306_PAGES) return;
    const uint8_t *bitmap = font_get_ascii(ch);
    for (uint8_t col = 0; col < 8; col++) {
        ssd1306_write_byte(page, x + col, bitmap[col]);
    }
}

/* 计算字符串像素宽度 (仅 ASCII, 每字符 8px) */
static int calc_str_width(const char *str)
{
    int w = 0;
    while (*str) {
        w += FONT_ASCII_W;
        str++;
    }
    return w;
}

/* 向 16px 高的指定区域绘制 ASCII 字符串 (跨两个 page) */
static void draw_str_16h(uint8_t x, uint8_t y, const char *str)
{
    uint8_t page = y >> 3; /* TOP page */
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

/* 刷新内容区 (不刷新状态栏) */
static void refresh_content_area(uint8_t fill_bg)
{
    uint8_t *buf = ssd1306_get_buffer();
    uint8_t start_page = DISP_CONTENT_Y >> 3;  /* page 2 */

    if (fill_bg) {
        for (uint8_t p = start_page; p < SSD1306_PAGES; p++) {
            memset(&buf[p * SSD1306_WIDTH], 0x00, SSD1306_WIDTH);
        }
    }
}

/* 在内容区绘制 16x16 中文字符 (跨两个 page) */
static void draw_chinese_char(uint8_t x, uint8_t page, const uint8_t *glyph)
{
    if (!glyph) return;
    uint8_t *buf = ssd1306_get_buffer();
    for (uint8_t c = 0; c < FONT_CHINESE_W && x + c < SSD1306_WIDTH; c++, x++) {
        if (page < SSD1306_PAGES)
            buf[page * SSD1306_WIDTH + x] = glyph[c * 2];       /* 上半 8 像素 */
        if (page + 1 < SSD1306_PAGES)
            buf[(page + 1) * SSD1306_WIDTH + x] = glyph[c * 2 + 1]; /* 下半 8 像素 */
    }
}

/* 在内容区绘制文字 (用于静态/翻页模式) */
static void draw_text_content(const char *text)
{
    refresh_content_area(1);

    uint8_t page = DISP_CONTENT_Y >> 3; /* page 2 */
    uint8_t col = 0;
    const char *p = text;
    uint8_t *buf = ssd1306_get_buffer();

    while (*p && page < SSD1306_PAGES - 1) {
        uint8_t first = (uint8_t)*p;

        /* 处理 UTF-8 中文 (3 字节, U+4E00~U+9FA5) */
        if ((first & 0x80) && (first & 0xF0) == 0xE0) {
            const uint8_t *glyph = font_get_chinese_utf8(p);

            /* 换行判断 */
            if (col + FONT_CHINESE_W > SSD1306_WIDTH) {
                col = 0;
                page += 2;
                if (page >= SSD1306_PAGES - 1) break;
            }

            if (glyph) {
                draw_chinese_char(col, page, glyph);
            } else {
                /* 字库未收录: 显示占位方块 */
                for (uint8_t c = 0; c < FONT_CHINESE_W && col + c < SSD1306_WIDTH; c++) {
                    if (page < SSD1306_PAGES)
                        buf[page * SSD1306_WIDTH + col + c] = 0xFF;
                    if (page + 1 < SSD1306_PAGES)
                        buf[(page + 1) * SSD1306_WIDTH + col + c] = 0xFF;
                }
            }
            col += FONT_CHINESE_W;
            p += 3;
        }
        /* ASCII (含 UTF-8 两字节/四字节等非中文字符, 统一按 ASCII 宽度占位) */
        else {
            if (col + FONT_ASCII_W > SSD1306_WIDTH) {
                col = 0;
                page += 2;
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
    /* 静态: 直接绘制内容 */
    draw_text_content(content_text);
}

static void effect_scroll_left(void)
{
    uint8_t *buf = ssd1306_get_buffer();
    uint8_t sp = DISP_CONTENT_Y >> 3;  /* start page 2 */
    uint8_t ep = SSD1306_PAGES - 1;    /* end page 7 */

    /* 将内容区每个 page 的 128 字节向左搬移 SCROLL_STEP_PX */
    for (uint8_t p = sp; p <= ep; p++) {
        uint8_t *line = &buf[p * SSD1306_WIDTH];
        memmove(line, line + SCROLL_STEP_PX, SSD1306_WIDTH - SCROLL_STEP_PX);
        /* 右侧填充 0 */
        memset(line + SSD1306_WIDTH - SCROLL_STEP_PX, 0, SCROLL_STEP_PX);
    }

    /* 在右侧新区域绘制下一段文字 (此处简化, 循环显示) */
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

    for (uint8_t p = sp; p <= ep; p++) {
        uint8_t *line = &buf[p * SSD1306_WIDTH];
        memmove(line + SCROLL_STEP_PX, line, SSD1306_WIDTH - SCROLL_STEP_PX);
        memset(line, 0, SCROLL_STEP_PX);
    }
}

static void effect_scroll_up(void)
{
    uint8_t *buf = ssd1306_get_buffer();
    uint8_t sp = DISP_CONTENT_Y >> 3;

    /* 将内容区向上搬移 SCROLL_STEP_PX 像素 */
    /* 简化实现: 逐 page 向上搬 */
    for (uint8_t p = sp; p < SSD1306_PAGES - 1; p++) {
        memcpy(&buf[p * SSD1306_WIDTH],
               &buf[(p + 1) * SSD1306_WIDTH],
               SSD1306_WIDTH);
    }
    memset(&buf[(SSD1306_PAGES - 1) * SSD1306_WIDTH], 0, SSD1306_WIDTH);
}

static void effect_scroll_down(void)
{
    uint8_t *buf = ssd1306_get_buffer();
    uint8_t sp = DISP_CONTENT_Y >> 3;

    for (uint8_t p = SSD1306_PAGES - 1; p > sp; p--) {
        memcpy(&buf[p * SSD1306_WIDTH],
               &buf[(p - 1) * SSD1306_WIDTH],
               SSD1306_WIDTH);
    }
    memset(&buf[sp * SSD1306_WIDTH], 0, SSD1306_WIDTH);
}

static void effect_flip(void)
{
    /* 翻页: 每隔 FLIP_INTERVAL_MS 切换显示 */
    /* 由 display_mgr_tick() 驱动 */
    /* 这里实际绘制在 tick 中处理 */
}

static void effect_fade(void)
{
    /* 淡入淡出: 通过调节 SSD1306 对比度实现 */
    /* 由 display_mgr_tick() 驱动 */
}

/* --- 状态栏绘制 --- */

/* 天气图标/文字映射 (8 ASCII 先简单用英文简写, 后续优化为自定义 5×7 图标) */
static const char* weather_labels[] = {
    "Sun", "Cld", "Ovc", "LtR", "HvR", "Trm", "Sno"
};

static void draw_status_bar(void)
{
    char line[32];
    uint8_t *buf = ssd1306_get_buffer();
    /* 清空状态栏 (page 0, 1) */
    memset(&buf[0], 0, SSD1306_WIDTH * 2);

    /* 第 0 行(page 0 上部): 时间 HH:MM:SS (8 字符, 64px) */
    snprintf(line, sizeof(line), "%02d:%02d:%02d",
             status.hour, status.minute, status.second);
    draw_str_16h(0, 0, line);

    /* 天气简写 (从 x=72 开始, 3 字符) */
    if (status.weather_type < 7) {
        draw_str_16h(72, 0, weather_labels[status.weather_type]);
    }

    /* 温度 (从 x=100, 如 "26C") */
    snprintf(line, sizeof(line), "%dC", (int)status.temperature);
    draw_str_16h(100, 0, line);

    /* 日期 (page 0 下部也可以, 或用第 2 page 的一部分)
     * 为简单, 日期和时间共用 page 0+1 的右侧空间。
     * 若需显示日期, 可在第 1 page 独立行绘制。
     */
}

/* --- 公开接口 --- */

void display_mgr_init(const char *boot_text_str)
{
    /* 注册特效 */
    effects[DISP_MODE_STATIC]   = effect_static;
    effects[DISP_MODE_SCROLL_L] = effect_scroll_left;
    effects[DISP_MODE_SCROLL_R] = effect_scroll_right;
    effects[DISP_MODE_SCROLL_U] = effect_scroll_up;
    effects[DISP_MODE_SCROLL_D] = effect_scroll_down;
    effects[DISP_MODE_FLIP]     = effect_flip;
    effects[DISP_MODE_FADE]     = effect_fade;

    /* 存储上电文字 */
    if (boot_text_str && boot_text_str[0]) {
        strncpy(boot_text, boot_text_str, BOOT_TEXT_MAX_LEN - 1);
    }
    strncpy(content_text, boot_text, CONTENT_MAX_LEN - 1);

    /* 默认静态模式 */
    current_mode = DISP_MODE_STATIC;

    /* 初始化内容区 */
    refresh_content_area(1);
    draw_text_content(content_text);

    /* 状态栏默认值 */
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
        content_pixel_width = calc_str_width(content_text);

        /* 切换文字后重置滚动偏移 */
        scroll_offset_x = 0;
        scroll_offset_y = 0;
        scroll_dirty = 1;

        /* 立即刷新 */
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

    /* 停止之前的特效状态 */
    ssd1306_scroll_stop();
    refresh_content_area(1);

    current_mode = mode;
    scroll_offset_x = 0;
    scroll_offset_y = 0;
    fade_step = 0;

    if (mode == DISP_MODE_STATIC) {
        draw_text_content(content_text);
    } else if (mode == DISP_MODE_FADE) {
        fade_dir = 1;  /* 先淡入 */
        fade_step = 0;
        ssd1306_set_contrast(0); /* 从最暗开始 */
        draw_text_content(content_text);
    }

    ssd1306_update_screen();
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

void display_mgr_update_status(const display_status_t *new_status)
{
    if (new_status) {
        memcpy(&status, new_status, sizeof(display_status_t));
    }
    draw_status_bar();
    ssd1306_update_screen();
}

const display_status_t* display_mgr_get_status(void)
{
    return &status;
}

void display_mgr_tick(void)
{
    static uint32_t tick_count = 0;
    tick_count++;

    /* 周期性触发显存同步上报 */
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
            /* 重新绘制状态栏 (滚动可能覆盖) */
            draw_status_bar();
            ssd1306_update_screen();
        }
        break;

    case DISP_MODE_FLIP:
        flip_timer += 50; /* 假设 tick 50ms */
        if (flip_timer >= FLIP_INTERVAL_MS) {
            flip_timer = 0;
            flip_phase ^= 1;
            refresh_content_area(1);
            if (flip_phase == 0) {
                draw_text_content(content_text);
            } else {
                /* 翻页可显示第二段文字 (此处简化, 循环) */
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
            if (fade_dir == 1) {  /* 淡入 */
                fade_step++;
                if (fade_step >= FADE_STEPS) {
                    fade_dir = 0;  /* 开始淡出 */
                }
            } else {  /* 淡出 */
                fade_step--;
                if (fade_step == 0) {
                    fade_dir = 1;
                }
            }
            uint8_t contrast = (uint8_t)((uint32_t)fade_step * 255 / FADE_STEPS);
            ssd1306_set_contrast(contrast);
        }
        break;

    case DISP_MODE_STATIC:
    default:
        break;
    }
}

void display_mgr_sync_frame(void)
{
    const uint8_t *frame_buf = ssd1306_get_buffer();
    uint16_t total = SSD1306_WIDTH * SSD1306_PAGES;  /* 1024 */
    uint8_t seg_total = (total + FRAME_SYNC_PAYLOAD - 1) / FRAME_SYNC_PAYLOAD;

    if (frame_sync_seg >= seg_total) {
        frame_sync_seg = 0;
    }

    uint16_t offset = frame_sync_seg * FRAME_SYNC_PAYLOAD;
    uint16_t remain = total - offset;
    uint8_t payload_len = (remain > FRAME_SYNC_PAYLOAD) ? FRAME_SYNC_PAYLOAD : (uint8_t)remain;

    uint8_t data[FRAME_SYNC_PAYLOAD + 2];
    data[0] = frame_sync_seg;
    data[1] = seg_total;
    memcpy(&data[2], &frame_buf[offset], payload_len);

    uint16_t len = proto_build_frame(CMD_FRAME_SYNC, frame_sync_seg, data, payload_len + 2);
    uart_drv_send(proto_get_tx_buf(), len);

    frame_sync_seg++;
}
