/**
 * @file    menu_mgr.c
 * @brief   多级菜单管理器实现 — 导航状态机 + OLED 渲染
 *
 * 128×64 OLED, 4 行 × 16px, 中英文混排。
 * 渲染复用 ssd1306 全局 buffer, 仅按键触发重绘。
 */

#include "menu_mgr.h"
#include "ssd1306.h"
#include "font.h"
#include "display_mgr.h"
#include <string.h>

/* menu_items.c 提供的接口 (跨编译单元, 无独立 .h) */
extern const menu_item_t* menu_items_get_root(void);
extern void menu_items_init_state(void);

/* --- 内部常量 --- */
#define MENU_ROWS_VISIBLE   4
#define MENU_ROW_HEIGHT_PX 16
#define MENU_ROW_PAGES      2
#define MENU_INDENT_PX     16   /* 非光标行缩进 2 个 ASCII 空格 / 1 个中文空格 */
#define MENU_ARROW_STR      ">"

/* --- 每层菜单上下文 (存于栈中) --- */
typedef struct {
    uint8_t cursor;
    uint8_t scroll_offset;
} menu_context_t;

/* --- 菜单管理器内部状态 --- */
typedef struct {
    const menu_item_t **current_menu;
    uint8_t            current_menu_count;
    uint8_t            cursor;
    uint8_t            scroll_offset;
    menu_context_t     stack[MENU_MAX_DEPTH];
    uint8_t            depth;
    bool               active;
    bool               dirty;
    bool               value_editing;
    uint8_t            value_backup;   /* VALUE编辑原始值备份, KEY4恢复用 */
    /* INFO 模式: 显示详情页 */
    bool               info_showing;
    const char        *info_detail;
} menu_state_t;

static menu_state_t g_menu;



/* ================================================================
 * 底层绘制: 在指定 page 位置绘制 ASCII 字符
 * ================================================================ */

/* 绘制 ASCII 字符 (正常模式: 白色文字) */
static uint8_t draw_ascii_normal(uint8_t page, uint8_t x, char ch)
{
    if (page >= SSD1306_PAGES - 1 || x >= SSD1306_WIDTH) return x;
    const uint8_t *bm = font_get_ascii(ch);
    uint8_t *buf = ssd1306_get_buffer();
    for (uint8_t c = 0; c < FONT_ASCII_W && x < SSD1306_WIDTH; c++, x++) {
        buf[page     * SSD1306_WIDTH + x] = bm[c];
        buf[(page + 1) * SSD1306_WIDTH + x] = bm[c + 8];
    }
    return x;
}

/* 绘制 ASCII 字符 (反白模式: 白底黑字, 擦除) */
static uint8_t draw_ascii_inverted(uint8_t page, uint8_t x, char ch)
{
    if (page >= SSD1306_PAGES - 1 || x >= SSD1306_WIDTH) return x;
    const uint8_t *bm = font_get_ascii(ch);
    uint8_t *buf = ssd1306_get_buffer();
    for (uint8_t c = 0; c < FONT_ASCII_W && x < SSD1306_WIDTH; c++, x++) {
        buf[page     * SSD1306_WIDTH + x] &= ~bm[c];
        buf[(page + 1) * SSD1306_WIDTH + x] &= ~bm[c + 8];
    }
    return x;
}

/* 绘制中文字符 (正常模式) */
static uint8_t draw_chinese_normal(uint8_t page, uint8_t x, const char *utf8)
{
    if (page >= SSD1306_PAGES - 1 || x >= SSD1306_WIDTH) return x;
    const uint8_t *glyph = font_get_chinese_utf8(utf8);
    uint8_t *buf = ssd1306_get_buffer();
    if (glyph) {
        for (uint8_t c = 0; c < FONT_CHINESE_W && x + c < SSD1306_WIDTH; c++, x++) {
            buf[page     * SSD1306_WIDTH + x] = glyph[c * 2];
            buf[(page + 1) * SSD1306_WIDTH + x] = glyph[c * 2 + 1];
        }
    } else {
        /* 缺字显示实心方块 */
        for (uint8_t c = 0; c < FONT_CHINESE_W && x + c < SSD1306_WIDTH; c++, x++) {
            buf[page     * SSD1306_WIDTH + x] = 0xFF;
            buf[(page + 1) * SSD1306_WIDTH + x] = 0xFF;
        }
    }
    return x;
}

/* 绘制中文字符 (反白模式: 擦除) */
static uint8_t draw_chinese_inverted(uint8_t page, uint8_t x, const char *utf8)
{
    if (page >= SSD1306_PAGES - 1 || x >= SSD1306_WIDTH) return x;
    const uint8_t *glyph = font_get_chinese_utf8(utf8);
    uint8_t *buf = ssd1306_get_buffer();
    if (glyph) {
        for (uint8_t c = 0; c < FONT_CHINESE_W && x + c < SSD1306_WIDTH; c++, x++) {
            buf[page     * SSD1306_WIDTH + x] &= ~glyph[c * 2];
            buf[(page + 1) * SSD1306_WIDTH + x] &= ~glyph[c * 2 + 1];
        }
    } else {
        /* 缺字擦除为黑色方块 */
        for (uint8_t c = 0; c < FONT_CHINESE_W && x + c < SSD1306_WIDTH; c++, x++) {
            buf[page     * SSD1306_WIDTH + x] = 0x00;
            buf[(page + 1) * SSD1306_WIDTH + x] = 0x00;
        }
    }
    return x;
}

/**
 * 绘制混合中英文文本
 * @return 绘制后的 x 坐标
 */
static uint8_t draw_text(uint8_t page, uint8_t x, const char *text, bool inverted)
{
    const char *p = text;
    while (*p && x < SSD1306_WIDTH) {
        uint8_t first = (uint8_t)*p;
        if ((first & 0x80) && (first & 0xF0) == 0xE0) {
            /* 3 字节 UTF-8 中文 */
            if (x + FONT_CHINESE_W > SSD1306_WIDTH) break;
            if (inverted)
                x = draw_chinese_inverted(page, x, p);
            else
                x = draw_chinese_normal(page, x, p);
            p += 3;
        } else if ((first & 0x80) && (first & 0xE0) == 0xC0) {
            /* 无效 2 字节序列, 跳过 */
            p += 2;
        } else {
            /* ASCII */
            if (x + FONT_ASCII_W > SSD1306_WIDTH) break;
            if (inverted)
                x = draw_ascii_inverted(page, x, *p);
            else
                x = draw_ascii_normal(page, x, *p);
            p++;
        }
    }
    return x;
}

/* ================================================================
 * 行级绘制: 填充行背景 + 绘制整行内容
 * ================================================================ */

/* 填充一整行 (2 pages) 为白色 (用于反白选中效果) */
static void fill_row_white(uint8_t row)
{
    uint8_t page = row * MENU_ROW_PAGES;
    uint8_t *buf = ssd1306_get_buffer();
    for (uint8_t x = 0; x < SSD1306_WIDTH; x++) {
        buf[page     * SSD1306_WIDTH + x] = 0xFF;
        buf[(page + 1) * SSD1306_WIDTH + x] = 0xFF;
    }
}

/* 绘制后缀: TOGGLE 的 [x]/[ ] 或 VALUE 的 [NNN] */
static void draw_suffix(uint8_t page, const menu_item_t *item, bool inverted)
{
    char suffix[16] = {0};
    uint8_t suffix_w = 0;

    if (item->type == MENU_TYPE_TOGGLE) {
        uint8_t val = *(item->toggle.value_ptr);
        bool checked = (item->toggle.checked_value == 0)
                       ? (val == 0)
                       : (val == item->toggle.checked_value);
        if (checked) {
            suffix[0] = '['; suffix[1] = 'x'; suffix[2] = ']'; suffix_w = 24;
        } else {
            suffix[0] = '['; suffix[1] = ' '; suffix[2] = ']'; suffix_w = 24;
        }
    } else if (item->type == MENU_TYPE_VALUE) {
        uint8_t val = *(item->value.value_ptr);
        /* 格式化为 3 位数字, 如 "[128]" */
        uint8_t idx = 0;
        suffix[idx++] = '[';
        if (val >= 100) suffix[idx++] = '0' + (val / 100);
        if (val >= 10)  suffix[idx++] = '0' + ((val / 10) % 10);
        suffix[idx++] = '0' + (val % 10);
        suffix[idx++] = ']';
        suffix_w = idx * FONT_ASCII_W;
    }

    if (suffix_w > 0) {
        uint8_t x = SSD1306_WIDTH - suffix_w;
        if (x < 8) x = 8;
        draw_text(page, x, suffix, inverted);
    }
}

/* 绘制滚动指示器: "…… (共N项)" */


/* ================================================================
 * 文本像素宽度测量 (供 render_menu 使用)
 * ================================================================ */

static uint8_t measure_text_width(const char *text)
{
    const char *p = text;
    uint8_t w = 0;
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

static uint8_t get_suffix_width(const menu_item_t *item)
{
    if (item->type == MENU_TYPE_TOGGLE) {
        return 24;  /* [x] 或 [ ] */
    } else if (item->type == MENU_TYPE_VALUE) {
        uint8_t val = *(item->value.value_ptr);
        if (val >= 100) return 40;   /* [NNN] */
        if (val >= 10)  return 32;   /* [ NN] */
        return 24;                    /* [  N] */
    }
    return 0;
}

/* ================================================================
 * 菜单渲染
 * ================================================================ */

static void render_menu(void)
{
    uint8_t *buf = ssd1306_get_buffer();
    memset(buf, 0x00, SSD1306_WIDTH * SSD1306_PAGES);

    for (uint8_t row = 0; row < MENU_ROWS_VISIBLE; row++) {
        uint8_t item_idx = g_menu.scroll_offset + row;
        if (item_idx >= g_menu.current_menu_count) break;

        const menu_item_t *item = g_menu.current_menu[item_idx];
        uint8_t page = row * MENU_ROW_PAGES;

        if (item_idx == g_menu.cursor) {
            /* 光标选中行: 反白显示 */
            fill_row_white(row);
            /* "> " 箭头始终在固定位置 */
            draw_text(page, 0, MENU_ARROW_STR, true);
            uint8_t x = draw_ascii_inverted(page, 8, ' ');  /* 间距 */
            /* 长文本自动左移, 使右端贴齐屏幕右侧 */
            uint8_t text_w = measure_text_width(item->text);
            uint8_t suffix_w = get_suffix_width(item);
            uint8_t space_for_text = SSD1306_WIDTH - x - suffix_w;
            if (text_w > space_for_text) {
                int16_t new_x = (int16_t)SSD1306_WIDTH - (int16_t)suffix_w - (int16_t)text_w;
                x = (new_x < 16) ? 16 : (uint8_t)new_x;
            }
            /* 菜单项文字 */
            x = draw_text(page, x, item->text, true);
            /* 右侧后缀 */
            draw_suffix(page, item, true);
        } else {
            /* 普通行: 长文本也右对齐 */
            uint8_t text_w = measure_text_width(item->text);
            uint8_t suffix_w = get_suffix_width(item);
            uint8_t space_for_text = SSD1306_WIDTH - MENU_INDENT_PX - suffix_w;
            uint8_t x = MENU_INDENT_PX;
            if (text_w > space_for_text) {
                int16_t new_x = (int16_t)SSD1306_WIDTH - (int16_t)suffix_w - (int16_t)text_w;
                x = (new_x < (int16_t)MENU_INDENT_PX) ? MENU_INDENT_PX : (uint8_t)new_x;
            }
            x = draw_text(page, x, item->text, false);
            draw_suffix(page, item, false);
        }
    }
	
//暂时删除角标显示	
#if 0
    /* 角标滚动指示器: 底部有更多项时在右下角显示 "↓", 已滚动时显示 "▲" */
    if (g_menu.current_menu_count > MENU_ROWS_VISIBLE) {
        uint8_t indic_page = (MENU_ROWS_VISIBLE - 1) * MENU_ROW_PAGES;
        uint8_t indic_x    = SSD1306_WIDTH - FONT_ASCII_W;
        if (g_menu.scroll_offset > 0) {
            /* 上方还有内容: ▲ */
            draw_ascii_normal(indic_page, indic_x, 0x5E)  /* '^' */;
        }
        if (g_menu.current_menu_count - g_menu.scroll_offset > MENU_ROWS_VISIBLE) {
            /* 下方还有内容: ↓ */
            if (g_menu.scroll_offset > 0) indic_x -= FONT_ASCII_W;
            draw_ascii_normal(indic_page, indic_x, 0x76)  /* 'v' */;
        }
    }
#endif
}

/* ================================================================
 * INFO 详情页渲染 (全屏显示 detail_text)
 * ================================================================ */

static void render_info(void)
{
    uint8_t *buf = ssd1306_get_buffer();
    memset(buf, 0x00, SSD1306_WIDTH * SSD1306_PAGES);

    if (!g_menu.info_detail) return;

    const char *p = g_menu.info_detail;
    uint8_t page = 0;
    uint8_t col  = 0;

    while (*p && page < SSD1306_PAGES - 1) {
        uint8_t first = (uint8_t)*p;
        if ((first & 0x80) && (first & 0xF0) == 0xE0) {
            if (col + FONT_CHINESE_W > SSD1306_WIDTH) {
                col = 0; page += 2;
                if (page >= SSD1306_PAGES - 1) break;
            }
            col = draw_chinese_normal(page, col, p);
            p += 3;
        } else if ((first & 0x80) && (first & 0xE0) == 0xC0) {
            p += 2;
        } else {
            if (col + FONT_ASCII_W > SSD1306_WIDTH) {
                col = 0; page += 2;
                if (page >= SSD1306_PAGES - 1) break;
            }
            col = draw_ascii_normal(page, col, *p);
            p++;
        }
    }
}

/* ================================================================
 * 滚动逻辑: 确保光标在可见范围内
 * ================================================================ */

static void adjust_scroll(void)
{
    if (g_menu.current_menu_count <= MENU_ROWS_VISIBLE) {
        g_menu.scroll_offset = 0;
        return;
    }

    if (g_menu.cursor < g_menu.scroll_offset) {
        g_menu.scroll_offset = g_menu.cursor;
    } else if (g_menu.cursor >= g_menu.scroll_offset + MENU_ROWS_VISIBLE) {
        g_menu.scroll_offset = g_menu.cursor - MENU_ROWS_VISIBLE + 1;
    }

    /* 确保不会滚过头 */
    if (g_menu.scroll_offset + MENU_ROWS_VISIBLE > g_menu.current_menu_count) {
        g_menu.scroll_offset = g_menu.current_menu_count - MENU_ROWS_VISIBLE;
    }
}

/* ================================================================
 * 进入子菜单
 * ================================================================ */

static void enter_submenu(const menu_item_t *item)
{
    if (item->type != MENU_TYPE_SUBMENU) return;

    /* 保存当前层状态到栈 */
    if (g_menu.depth < MENU_MAX_DEPTH) {
        g_menu.stack[g_menu.depth].cursor        = g_menu.cursor;
        g_menu.stack[g_menu.depth].scroll_offset = g_menu.scroll_offset;
    }

    g_menu.depth++;
    g_menu.current_menu       = item->submenu.items;
    g_menu.current_menu_count = item->submenu.count;
    g_menu.cursor             = 0;
    g_menu.scroll_offset      = 0;
    g_menu.dirty              = true;
}

/* 返回上级菜单 */
static void exit_submenu(void)
{
    if (g_menu.depth == 0) return;

    g_menu.depth--;
    if (g_menu.depth < MENU_MAX_DEPTH) {
        g_menu.cursor        = g_menu.stack[g_menu.depth].cursor;
        g_menu.scroll_offset = g_menu.stack[g_menu.depth].scroll_offset;
    }

    /* 重新定位 current_menu: 从根开始沿栈遍历 */
    const menu_item_t *root = menu_items_get_root();
    g_menu.current_menu       = root->submenu.items;
    g_menu.current_menu_count = root->submenu.count;

    for (uint8_t d = 0; d < g_menu.depth; d++) {
        uint8_t idx = g_menu.stack[d].cursor;
        if (idx < g_menu.current_menu_count) {
            const menu_item_t *parent = g_menu.current_menu[idx];
            if (parent->type == MENU_TYPE_SUBMENU) {
                g_menu.current_menu       = parent->submenu.items;
                g_menu.current_menu_count = parent->submenu.count;
            }
        }
    }

    /* 恢复滚动位置 */
    if (g_menu.depth < MENU_MAX_DEPTH) {
        g_menu.scroll_offset = g_menu.stack[g_menu.depth].scroll_offset;
    }

    adjust_scroll();
    g_menu.dirty = true;
}

/* 返回根菜单 */
static void goto_root(void)
{
    const menu_item_t *root = menu_items_get_root();
    g_menu.depth             = 0;
    g_menu.current_menu       = root->submenu.items;
    g_menu.current_menu_count = root->submenu.count;
    g_menu.cursor             = 0;
    g_menu.scroll_offset      = 0;
    g_menu.value_editing      = false;
    g_menu.info_showing       = false;
    g_menu.dirty              = true;
}

/* ================================================================
 * 光标移动
 * ================================================================ */
static void cursor_up(void)
{
    if (g_menu.current_menu_count == 0) return;
    if (g_menu.cursor > 0) {
        g_menu.cursor--;
    }
    /* 已在顶部, 不再回绕 */
    adjust_scroll();
    g_menu.dirty = true;
}

static void cursor_down(void)
{
    if (g_menu.current_menu_count == 0) return;
    if (g_menu.cursor + 1 < g_menu.current_menu_count) {
        g_menu.cursor++;
    }
    /* 已在底部, 不再回绕 */
    adjust_scroll();
    g_menu.dirty = true;
}

/* ================================================================
 * 确认操作 (KEY3)
 * ================================================================ */

static void handle_confirm(void)
{
    const menu_item_t *item = g_menu.current_menu[g_menu.cursor];

    switch (item->type) {
    case MENU_TYPE_SUBMENU:
        enter_submenu(item);
        break;

    case MENU_TYPE_TOGGLE: {
        uint8_t *vp = item->toggle.value_ptr;
        *vp = (*vp == item->toggle.checked_value) ? (uint8_t)(!item->toggle.checked_value) : item->toggle.checked_value;
        if (item->toggle.on_change) {
            item->toggle.on_change(*vp);
        }
        g_menu.dirty = true;
        break;
    }

    case MENU_TYPE_VALUE:
        /* 进入数值编辑模式 */
        g_menu.value_backup = *(item->value.value_ptr);  /* 备份原始值 */
        g_menu.value_editing = true;
        g_menu.dirty = true;
        break;

    case MENU_TYPE_ACTION:
        if (item->action) {
            item->action();
        }
        break;

    case MENU_TYPE_INFO:
        /* 进入 INFO 详情页 */
        g_menu.info_showing = true;
        g_menu.info_detail  = item->info.detail_text;
        g_menu.dirty = true;
        break;
    }
}

/* ================================================================
 * VALUE 编辑: KEY1=增, KEY2=减
 * ================================================================ */

static void value_edit_up(void)
{
    if (!g_menu.value_editing) return;
    const menu_item_t *item = g_menu.current_menu[g_menu.cursor];
    if (item->type != MENU_TYPE_VALUE) return;

    uint8_t *vp = item->value.value_ptr;
    if (*vp + item->value.step <= item->value.max) {
        *vp += item->value.step;
    } else {
        *vp = item->value.max;
    }
    if (item->value.on_change) {
        item->value.on_change(*vp);
    }
    g_menu.dirty = true;
}

static void value_edit_down(void)
{
    if (!g_menu.value_editing) return;
    const menu_item_t *item = g_menu.current_menu[g_menu.cursor];
    if (item->type != MENU_TYPE_VALUE) return;

    uint8_t *vp = item->value.value_ptr;
    if (*vp >= item->value.step + item->value.min) {
        *vp -= item->value.step;
    } else {
        *vp = item->value.min;
    }
    if (item->value.on_change) {
        item->value.on_change(*vp);
    }
    g_menu.dirty = true;
}

/* ================================================================
 * 按键分发
 * ================================================================ */

void menu_mgr_handle_key(uint8_t key_id, key_event_t event)
{
    /* INFO 显示中: 任意键退出 */
    if (g_menu.info_showing) {
        if (event == KEY_EVENT_SHORT_PRESS) {
            g_menu.info_showing = false;
            g_menu.dirty = true;
        }
        return;
    }

    /* VALUE 编辑中 */
    if (g_menu.value_editing) {
        switch (event) {
        case KEY_EVENT_NONE:
            break;
        case KEY_EVENT_LONG_PRESS:
        case KEY_EVENT_RELEASE:
        case KEY_EVENT_SHORT_PRESS:
        case KEY_EVENT_LONG_PRESS_REPEAT:
            if (key_id == 1) value_edit_down();
            if (key_id == 2) value_edit_up();
            break;
        }
        /* KEY3 确认 / KEY4 取消 → 退出编辑 */
        if (event == KEY_EVENT_SHORT_PRESS) {
            if (key_id == 3) {
                /* 确认: 保持当前值, 退出编辑 */
                g_menu.value_editing = false;
                g_menu.dirty = true;
            } else if (key_id == 4) {
                /* 返回: 恢复原始值 */
                const menu_item_t *item = g_menu.current_menu[g_menu.cursor];
                if (item->type == MENU_TYPE_VALUE) {
                    *(item->value.value_ptr) = g_menu.value_backup;
                    if (item->value.on_change) {
                        item->value.on_change(*(item->value.value_ptr));
                    }
                }
                g_menu.value_editing = false;
                g_menu.dirty = true;
            }
        }
        return;
    }

    /* 正常菜单导航 */
    switch (event) {
    case KEY_EVENT_SHORT_PRESS:
    case KEY_EVENT_LONG_PRESS_REPEAT:
        if (key_id == 1) {
            cursor_down();
        } else if (key_id == 2) {
            cursor_up();
        } else if (key_id == 3 && event == KEY_EVENT_SHORT_PRESS) {
            handle_confirm();
        } else if (key_id == 4 && event == KEY_EVENT_SHORT_PRESS) {
            if (g_menu.depth == 0) {
                /* 根菜单: 退出 */
                menu_mgr_deactivate();
            } else {
                exit_submenu();
            }
        }
        break;

    case KEY_EVENT_LONG_PRESS:
        /* KEY4 长按: 返回根菜单 / 退出 */
        if (key_id == 4) {
            if (g_menu.depth == 0) {
                menu_mgr_deactivate();
            } else {
                goto_root();
            }
        }
        break;

    case KEY_EVENT_NONE:
    case KEY_EVENT_RELEASE:
        break;
    }
}

/* ================================================================
 * 每帧 tick
 * ================================================================ */

void menu_mgr_tick(void)
{
    if (!g_menu.active) return;

    if (g_menu.dirty) {
        if (g_menu.info_showing) {
            render_info();
        } else {
            render_menu();
        }
        ssd1306_update_screen();
        g_menu.dirty = false;
    }
}

/* ================================================================
 * 公开 API 实现
 * ================================================================ */

void menu_mgr_init(void)
{
    memset(&g_menu, 0, sizeof(g_menu));

    /* 同步外部系统状态到菜单变量 */
    menu_items_init_state();

    /* 从 menu_items 获取根菜单 */
    const menu_item_t *root = menu_items_get_root();
    g_menu.current_menu       = root->submenu.items;
    g_menu.current_menu_count = root->submenu.count;
    g_menu.cursor             = 0;
    g_menu.scroll_offset      = 0;
    g_menu.depth              = 0;
    g_menu.active             = true;
    g_menu.dirty              = true;
    g_menu.value_editing      = false;
    g_menu.info_showing       = false;

    /* 通知 display_mgr 抑制远程帧刷屏 */
    display_mgr_set_menu_suppress(true);
}

bool menu_mgr_is_active(void)
{
    return g_menu.active;
}

void menu_mgr_activate(void)
{
    if (g_menu.active) return;

    /* 同步外部状态 */
    menu_items_init_state();

    /* 进入根菜单 */
    const menu_item_t *root = menu_items_get_root();
    g_menu.current_menu       = root->submenu.items;
    g_menu.current_menu_count = root->submenu.count;
    g_menu.cursor             = 0;
    g_menu.scroll_offset      = 0;
    g_menu.depth              = 0;
    g_menu.active             = true;
    g_menu.dirty              = true;
    g_menu.value_editing      = false;
    g_menu.info_showing       = false;

    memset(g_menu.stack, 0, sizeof(g_menu.stack));

    display_mgr_set_menu_suppress(true);
}

void menu_mgr_deactivate(void)
{
    g_menu.active = false;
    g_menu.dirty  = false;
    g_menu.info_showing = false;
    g_menu.value_editing = false;

    display_mgr_redraw();
}

bool menu_mgr_is_dirty(void)
{
    return g_menu.dirty;
}
