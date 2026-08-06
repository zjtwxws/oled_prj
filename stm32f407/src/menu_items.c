/**
 * @file    menu_items.c
 * @brief   菜单树静态数据定义 (全部 const, 存于 Flash)
 *
 * 菜单结构:
 *   主菜单 (8项)
 *   ├── 工作模式 → 本地 / 远程 (TOGGLE 互斥)
 *   ├── 显示内容 → 时间 / 天气 / 日期 / 自定义文字 (ACTION)
 *   ├── 显示特效 → 静态 / 左滚 / 右滚 / 上滚 / 下滚 / 翻页 / 淡入淡出 (ACTION)
 *   ├── 设置 → 对比度设置 → 对比度 (VALUE 0~255)
 *   ├── LED控制 → 关闭 / 常亮 / 闪烁 (ACTION)
 *   ├── 上电文字 (INFO, 预留)
 *   ├── 系统信息 → 固件版本 / 运行时间 (INFO)
 *   └── 预留 → 三级示例 → 选项A / 选项B / 选项C (ACTION)
 */

#include "menu_mgr.h"
#include "display_mgr.h"
#include "ssd1306.h"
#include "led_mgr.h"
#include "sys_config.h"
#include "debug_console.h"
#include "user_app.h"

/* ================================================================
 * 菜单共享状态变量 (RAM, 被 TOGGLE/VALUE 的 value_ptr 指向)
 * ================================================================ */

static uint8_t g_remote_mode = 0;    /* 0=本地, 1=远程 */
static uint8_t g_contrast    = 255;

/* ================================================================
 * 回调函数 — 桥接到现有系统接口
 * ================================================================ */

/* --- 工作模式 TOGGLE 回调 --- */
static void cb_mode_changed(uint8_t new_val)
{
    display_mgr_set_remote(new_val != 0);
}

/* --- 显示内容 ACTION 回调 --- */
static void cb_disp_time(void)
{
    display_mgr_set_remote(true);
    display_mgr_set_sub_mode(REMOTE_SUB_TIME);
}

static void cb_disp_weather(void)
{
    display_mgr_set_remote(true);
    display_mgr_set_sub_mode(REMOTE_SUB_WEATHER);
}

static void cb_disp_date(void)
{
    display_mgr_set_remote(true);
    display_mgr_set_sub_mode(REMOTE_SUB_DATE);
}

static void cb_disp_custom(void)
{
    display_mgr_set_sub_mode(REMOTE_SUB_TEXT);
}

/* --- 显示特效 ACTION 回调 --- */
static void cb_effect_static(void)   { display_mgr_set_mode(DISP_MODE_STATIC); }
static void cb_effect_scroll_l(void) { display_mgr_set_mode(DISP_MODE_SCROLL_L); }
static void cb_effect_scroll_r(void) { display_mgr_set_mode(DISP_MODE_SCROLL_R); }
static void cb_effect_scroll_u(void) { display_mgr_set_mode(DISP_MODE_SCROLL_U); }
static void cb_effect_scroll_d(void) { display_mgr_set_mode(DISP_MODE_SCROLL_D); }
static void cb_effect_flip(void)     { display_mgr_set_mode(DISP_MODE_FLIP); }
static void cb_effect_fade(void)     { display_mgr_set_mode(DISP_MODE_FADE); }

/* --- 对比度 VALUE 回调 --- */
static void cb_contrast_changed(uint8_t val)
{
    ssd1306_set_contrast(val);
}

/* --- LED 控制 ACTION 回调 --- */
static void cb_led_off(void)
{
    led_mgr_set_state(LED_STATE_OFF);
}

static void cb_led_on(void)
{
    led_mgr_set_state(LED_STATE_ON);
}

static void cb_led_blink(void)
{
    led_mgr_set_state(LED_STATE_BLINK);
}

/* --- 三级示例 ACTION 回调 --- */
static void cb_demo_a(void) { DEBUG_PRINTF("menu: demo level3 - option A"); }
static void cb_demo_b(void) { DEBUG_PRINTF("menu: demo level3 - option B"); }
static void cb_demo_c(void) { DEBUG_PRINTF("menu: demo level3 - option C"); }

/* ================================================================
 * 菜单项定义 (自底向上, const 存储于 Flash)
 * ================================================================ */

/* ---- 三级示例 (Level 2) ---- */
static const menu_item_t item_demo_a = {
    .text = "选项" "A",  /* "选项A" */
    .type = MENU_TYPE_ACTION,
    .action = cb_demo_a,
};
static const menu_item_t item_demo_b = {
    .text = "选项" "B",  /* "选项B" */
    .type = MENU_TYPE_ACTION,
    .action = cb_demo_b,
};
static const menu_item_t item_demo_c = {
    .text = "选项" "C",  /* "选项C" */
    .type = MENU_TYPE_ACTION,
    .action = cb_demo_c,
};
static const menu_item_t *menu_demo_items[] = {
    &item_demo_a, &item_demo_b, &item_demo_c,
};

/* ---- 预留 子菜单 (Level 1) ---- */
static const menu_item_t item_demo = {
    .text = "三级示例",  /* "三级示例" */
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_demo_items, .count = 3 },
};

/* ---- 系统信息 (Level 1) ---- */
static const menu_item_t item_fw_ver = {
    .text = "固件版本",  /* "固件版本" */
    .type = MENU_TYPE_INFO,
    .info = { .detail_text = FW_VERSION },
};
static const menu_item_t item_runtime = {
    .text = "运行时间",  /* "运行时间" */
    .type = MENU_TYPE_INFO,
    .info = { .detail_text = "--:--:--" },
};

/* ---- LED 控制 (Level 1) ---- */
static const menu_item_t item_led_off = {
    .text = "关闭",    /* "关闭" */
    .type = MENU_TYPE_ACTION,
    .action = cb_led_off,
};
static const menu_item_t item_led_on = {
    .text = "常亮",    /* "常亮" */
    .type = MENU_TYPE_ACTION,
    .action = cb_led_on,
};
static const menu_item_t item_led_blink = {
    .text = "闪烁",    /* "闪烁" */
    .type = MENU_TYPE_ACTION,
    .action = cb_led_blink,
};

/* ---- 显示特效 (Level 1) ---- */
static const menu_item_t item_effect_static = {
    .text = "静态",    /* "静态" */
    .type = MENU_TYPE_ACTION,
    .action = cb_effect_static,
};
static const menu_item_t item_effect_scroll_l = {
    .text = "左滚",    /* "左滚" */
    .type = MENU_TYPE_ACTION,
    .action = cb_effect_scroll_l,
};
static const menu_item_t item_effect_scroll_r = {
    .text = "右滚",    /* "右滚" */
    .type = MENU_TYPE_ACTION,
    .action = cb_effect_scroll_r,
};
static const menu_item_t item_effect_scroll_u = {
    .text = "上滚",    /* "上滚" */
    .type = MENU_TYPE_ACTION,
    .action = cb_effect_scroll_u,
};
static const menu_item_t item_effect_scroll_d = {
    .text = "下滚",    /* "下滚" */
    .type = MENU_TYPE_ACTION,
    .action = cb_effect_scroll_d,
};
static const menu_item_t item_effect_flip = {
    .text = "翻页",    /* "翻页" */
    .type = MENU_TYPE_ACTION,
    .action = cb_effect_flip,
};
static const menu_item_t item_effect_fade = {
    .text = "淡入淡出",  /* "淡入淡出" */
    .type = MENU_TYPE_ACTION,
    .action = cb_effect_fade,
};

/* ---- 显示内容 (Level 1) ---- */
static const menu_item_t item_content_time = {
    .text = "时间",    /* "时间" */
    .type = MENU_TYPE_ACTION,
    .action = cb_disp_time,
};
static const menu_item_t item_content_weather = {
    .text = "天气",    /* "天气" */
    .type = MENU_TYPE_ACTION,
    .action = cb_disp_weather,
};
static const menu_item_t item_content_date = {
    .text = "日期",    /* "日期" */
    .type = MENU_TYPE_ACTION,
    .action = cb_disp_date,
};
static const menu_item_t item_content_custom = {
    .text = "自定义文字",  /* "自定义文字" */
    .type = MENU_TYPE_ACTION,
    .action = cb_disp_custom,
};

/* ---- 工作模式 TOGGLE (Level 1) ---- */
static const menu_item_t item_mode_local = {
    .text = "本地",    /* "本地" */
    .type = MENU_TYPE_TOGGLE,
    .toggle = { .value_ptr = &g_remote_mode, .checked_value = 0, .on_change = cb_mode_changed },
};
static const menu_item_t item_mode_remote = {
    .text = "远程",    /* "远程" */
    .type = MENU_TYPE_TOGGLE,
    .toggle = { .value_ptr = &g_remote_mode, .checked_value = 1, .on_change = cb_mode_changed },
};

/* ---- 对比度 VALUE 项 (Level 2, 位于"对比度设置"子菜单下) ---- */
static const menu_item_t item_contrast = {
    .text = "对比度",
    .type = MENU_TYPE_VALUE,
    .value = { .value_ptr = &g_contrast, .min = 0, .max = 255, .step = 5,
               .on_change = cb_contrast_changed },
};

/* ---- 对比度设置 三级菜单 (Level 2) ---- */
static const menu_item_t *menu_contrast_items[] = {
    &item_contrast,
};

/* ---- 对比度设置 二级菜单 (Level 1, 位于"设置"下) ---- */
static const menu_item_t item_contrast_menu = {
    .text = "对比度设置",
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_contrast_items, .count = 1 },
};

/* ---- 设置 二级菜单 (Level 1) ---- */
static const menu_item_t *menu_setting_items[] = {
    &item_contrast_menu,
};

/* ================================================================
 * 子菜单数组 (Level 1 各组)
 * ================================================================ */

static const menu_item_t *menu_mode_items[] = {
    &item_mode_local, &item_mode_remote,
};

static const menu_item_t *menu_content_items[] = {
    &item_content_time, &item_content_weather,
    &item_content_date, &item_content_custom,
};

static const menu_item_t *menu_effect_items[] = {
    &item_effect_static, &item_effect_scroll_l, &item_effect_scroll_r,
    &item_effect_scroll_u, &item_effect_scroll_d,
    &item_effect_flip, &item_effect_fade,
};

static const menu_item_t *menu_led_items[] = {
    &item_led_off, &item_led_on, &item_led_blink,
};

static const menu_item_t *menu_sysinfo_items[] = {
    &item_fw_ver, &item_runtime,
};

static const menu_item_t *menu_reserved_items[] = {
    &item_demo,
};

/* ================================================================
 * 主菜单 (Level 0) — 8 个顶层项
 * ================================================================ */

static const menu_item_t item_main_1 = {
    .text = "1.工作模式",  /* "1.工作模式" */
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_mode_items, .count = 2 },
};
static const menu_item_t item_main_2 = {
    .text = "2.显示内容",  /* "2.显示内容" */
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_content_items, .count = 4 },
};
static const menu_item_t item_main_3 = {
    .text = "3.显示特效",  /* "3.显示特效" */
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_effect_items, .count = 7 },
};

static const menu_item_t item_main_4 = {
    .text = "4.设置",/* "4.设置" */
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_setting_items, .count = 1 },
};

static const menu_item_t item_main_5 = {
    .text = "5.LED控制",  /* "5.LED控制" */
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_led_items, .count = 3 },
};

static const menu_item_t item_main_6 = {
    .text = "6.上电文字",  /* "6.上电文字" */
    .type = MENU_TYPE_INFO,
    .info = { .detail_text = "预留功能" },  /* "预留功能" */
};

static const menu_item_t item_main_7 = {
    .text = "7.系统信息",  /* "7.系统信息" */
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_sysinfo_items, .count = 2 },
};
static const menu_item_t item_main_8 = {
    .text = "8.预留",  /* "8.预留" */
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_reserved_items, .count = 1 },
};

static const menu_item_t *menu_main_items[] = {
    &item_main_1, &item_main_2, &item_main_3,
    &item_main_4, &item_main_5, &item_main_6,
    &item_main_7, &item_main_8,
};

/* ================================================================
 * 根节点 (虚拟 SUBMENU, 含 8 个主菜单项)
 * ================================================================ */

static const menu_item_t menu_root = {
    .text = NULL,  /* 根节点不显示 */
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_main_items, .count = 8 },
};

/* ================================================================
 * 公开接口
 * ================================================================ */

/**
 * @brief  获取菜单根节点
 * @return 指向根菜单项的指针
 */
const menu_item_t* menu_items_get_root(void)
{
    return &menu_root;
}

/**
 * @brief  初始化菜单状态 (同步外部系统状态到菜单变量)
 */
void menu_items_init_state(void)
{
    g_remote_mode = display_mgr_is_remote() ? 1 : 0;
    /* g_contrast 保持当前值不变，不硬编码重置 */
}
