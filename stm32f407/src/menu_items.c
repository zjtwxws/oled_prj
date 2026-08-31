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
 *   ├── 上电文字 → 欢迎语 / Logo / 大号文字 (PREVIEW, 预览+确认选定)
 *   ├── 系统信息 → 固件版本 / 运行时间 (INFO)
 *   └── 预留 → 三级示例 → 选项A / 选项B / 选项C (ACTION)
 */

#include "menu_mgr.h"
#include "display_mgr.h"
#include "ssd1306.h"
#include "font.h"
#include "led_mgr.h"
#include "sys_config.h"
#include "app_fw_info.h"
#include "debug_console.h"
#include "user_app.h"
#include "app_ipc.h"
#include <string.h>

/* ================================================================
 * 菜单共享状态变量 (RAM, 被 TOGGLE/VALUE 的 value_ptr 指向)
 * ================================================================ */

static uint8_t g_remote_mode = 0;    /* 0=本地模式显示，1=远程模式显示 */
static uint8_t g_contrast    = 255;  /* OLED 对比度值 (0~255)，通过 VALUE 菜单项调节 */
static uint8_t g_poweron_type = SYS_CONFIG_POWERON_WELCOME;  /* 当前选中的上电显示类型 */

/* ================================================================
 * 回调函数 — 桥接到现有系统接口
 * ================================================================ */

/* --- 工作模式 TOGGLE 回调 --- */
/**
 * @brief  工作模式切换回调
 * @param  new_val 参数说明
 * @date   2026-08-07
 */
static void cb_mode_changed(uint8_t new_val)
{
    disp_cmd_t cmd = {0};
    cmd.type = DISP_CMD_SET_REMOTE;
    cmd.u.remote = (new_val != 0U);
    (void)app_ipc_send_disp_cmd(&cmd, 0);
}

/* --- 显示内容 ACTION 回调 --- */
/**
 * @brief  显示时间回调
 * @date   2026-08-07
 */
static void cb_disp_time(void)
{
    disp_cmd_t cmd = {0};
    cmd.type = DISP_CMD_SET_REMOTE_SUB;
    cmd.u.remote_sub.remote = true;
    cmd.u.remote_sub.sub_mode = REMOTE_SUB_TIME;
    (void)app_ipc_send_disp_cmd(&cmd, 0);
}

/**
 * @brief  显示天气回调
 * @date   2026-08-07
 */
static void cb_disp_weather(void)
{
    disp_cmd_t cmd = {0};
    cmd.type = DISP_CMD_SET_REMOTE_SUB;
    cmd.u.remote_sub.remote = true;
    cmd.u.remote_sub.sub_mode = REMOTE_SUB_WEATHER;
    (void)app_ipc_send_disp_cmd(&cmd, 0);
}

/**
 * @brief  显示日期回调
 * @date   2026-08-07
 */
static void cb_disp_date(void)
{
    disp_cmd_t cmd = {0};
    cmd.type = DISP_CMD_SET_REMOTE_SUB;
    cmd.u.remote_sub.remote = true;
    cmd.u.remote_sub.sub_mode = REMOTE_SUB_DATE;
    (void)app_ipc_send_disp_cmd(&cmd, 0);
}

/**
 * @brief  显示自定义文字回调
 * @date   2026-08-07
 */
static void cb_disp_custom(void)
{
    disp_cmd_t cmd = {0};
    cmd.type = DISP_CMD_SET_REMOTE_SUB;
    cmd.u.remote_sub.remote = true;
    cmd.u.remote_sub.sub_mode = REMOTE_SUB_TEXT;
    (void)app_ipc_send_disp_cmd(&cmd, 0);
}

/* --- 显示特效 ACTION 回调 --- */
/**
 * @brief  静态特效回调
 * @date   2026-08-07
 */
static void cb_effect_static(void)
{
    disp_cmd_t cmd = {0};
    cmd.type = DISP_CMD_SET_MODE;
    cmd.u.mode = DISP_MODE_STATIC;
    (void)app_ipc_send_disp_cmd(&cmd, 0);
}
/**
 * @brief  左滚特效回调
 * @date   2026-08-07
 */
static void cb_effect_scroll_l(void)
{
    disp_cmd_t cmd = {0};
    cmd.type = DISP_CMD_SET_MODE;
    cmd.u.mode = DISP_MODE_SCROLL_L;
    (void)app_ipc_send_disp_cmd(&cmd, 0);
}
/**
 * @brief  右滚特效回调
 * @date   2026-08-07
 */
static void cb_effect_scroll_r(void)
{
    disp_cmd_t cmd = {0};
    cmd.type = DISP_CMD_SET_MODE;
    cmd.u.mode = DISP_MODE_SCROLL_R;
    (void)app_ipc_send_disp_cmd(&cmd, 0);
}
/**
 * @brief  上滚特效回调
 * @date   2026-08-07
 */
static void cb_effect_scroll_u(void)
{
    disp_cmd_t cmd = {0};
    cmd.type = DISP_CMD_SET_MODE;
    cmd.u.mode = DISP_MODE_SCROLL_U;
    (void)app_ipc_send_disp_cmd(&cmd, 0);
}
/**
 * @brief  下滚特效回调
 * @date   2026-08-07
 */
static void cb_effect_scroll_d(void)
{
    disp_cmd_t cmd = {0};
    cmd.type = DISP_CMD_SET_MODE;
    cmd.u.mode = DISP_MODE_SCROLL_D;
    (void)app_ipc_send_disp_cmd(&cmd, 0);
}
/**
 * @brief  翻页特效回调
 * @date   2026-08-07
 */
static void cb_effect_flip(void)
{
    disp_cmd_t cmd = {0};
    cmd.type = DISP_CMD_SET_MODE;
    cmd.u.mode = DISP_MODE_FLIP;
    (void)app_ipc_send_disp_cmd(&cmd, 0);
}
/**
 * @brief  淡入淡出特效回调
 * @date   2026-08-07
 */
static void cb_effect_fade(void)
{
    disp_cmd_t cmd = {0};
    cmd.type = DISP_CMD_SET_MODE;
    cmd.u.mode = DISP_MODE_FADE;
    (void)app_ipc_send_disp_cmd(&cmd, 0);
}

/* --- 对比度 VALUE 回调 --- */
/**
 * @brief  对比度变化回调
 * @param  val 参数说明
 * @date   2026-08-07
 */
static void cb_contrast_changed(uint8_t val)
{
    disp_cmd_t cmd = {0};
    cmd.type = DISP_CMD_SET_CONTRAST;
    cmd.u.contrast = val;
    (void)app_ipc_send_disp_cmd(&cmd, 0);
}

/* --- LED 控制 ACTION 回调 --- */
/**
 * @brief  LED 关闭回调
 * @date   2026-08-07
 */
static void cb_led_off(void)
{
    led_cmd_t cmd = {0};
    cmd.type = LED_CMD_SET_STATE;
    cmd.state = LED_STATE_OFF;
    (void)app_ipc_send_led_cmd(&cmd, 0);
}

/**
 * @brief  LED 常亮回调
 * @date   2026-08-07
 */
static void cb_led_on(void)
{
    led_cmd_t cmd = {0};
    cmd.type = LED_CMD_SET_STATE;
    cmd.state = LED_STATE_ON;
    (void)app_ipc_send_led_cmd(&cmd, 0);
}

/**
 * @brief  LED 闪烁回调
 * @date   2026-08-07
 */
static void cb_led_blink(void)
{
    led_cmd_t cmd = {0};
    cmd.type = LED_CMD_SET_STATE;
    cmd.state = LED_STATE_BLINK;
    (void)app_ipc_send_led_cmd(&cmd, 0);
}

/* --- 三级示例 ACTION 回调 --- */
/**
 * @brief  三级示例选项A回调
 * @date   2026-08-07
 */
static void cb_demo_a(void)
{
    DEBUG_PRINTF("menu: demo level3 - option A");
}
/**
 * @brief  三级示例选项B回调
 * @date   2026-08-07
 */
static void cb_demo_b(void)
{
    DEBUG_PRINTF("menu: demo level3 - option B");
}
/**
 * @brief  三级示例选项C回调
 * @date   2026-08-07
 */
static void cb_demo_c(void)
{
    DEBUG_PRINTF("menu: demo level3 - option C");
}

/* --- 上电文字 PREVIEW 回调 --- */

/* Logo 全屏位图 (128×64 = 1024 字节), 放置于 Flash */
const uint8_t logo_boot_bmp[1024] = 
{
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x80,0x80,0x80,0x00,0x00,0x00,0x00,0x00,0x80,0xC0,0x20,0x90,0x70,0x10,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0xE0,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x80,0xC0,0x60,0xA0,0x70,0x70,0x38,0x3C,0xDE,0xEB,
0xE5,0x03,0x01,0x01,0x00,0x00,0x70,0x38,0x0E,0x23,0x18,0x1C,0x16,0x02,0x02,0x0A,
0xC2,0x6A,0xE2,0x0E,0x04,0x84,0x8C,0x8C,0x8C,0xCC,0xF4,0x4C,0x38,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x20,0x10,0x08,0x86,0x41,0x12,0x01,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x10,0x38,0x08,0x08,0x28,0x08,0x08,0x30,0x10,
0x10,0x10,0x30,0x20,0x60,0x20,0x60,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x01,0x00,0x00,0x00,0x00,0x00,0x01,0xFF,
0x03,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x80,0xC0,0x40,0x40,0xC0,0x80,
0x03,0x0E,0xF3,0x25,0x01,0x81,0x01,0x01,0x00,0xF0,0xB0,0x20,0x40,0x80,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0xC0,0x60,0x20,0xA0,
0x20,0x90,0x38,0x5C,0xD2,0x0A,0x59,0x28,0x0C,0x04,0x05,0x24,0x04,0x00,0xA4,0x84,
0x14,0x00,0x14,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,
0xD0,0xF2,0xD8,0xCD,0x44,0x46,0x63,0x21,0x60,0x20,0x30,0x30,0x20,0x20,0x20,0x20,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,
0x0E,0x3F,0xFF,0x00,0x00,0x20,0x00,0x0C,0x1B,0x19,0x1C,0x04,0x02,0x01,0x41,0xC0,
0xC0,0x80,0x81,0x3F,0x80,0x02,0x50,0x00,0x00,0x00,0x00,0x01,0x02,0x09,0x07,0x1C,
0x70,0xC0,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x1E,0x3F,0x7B,0x71,0xA1,0x80,0xC0,
0x80,0x80,0x80,0x80,0x81,0x83,0x8E,0x99,0xF8,0xC0,0xE0,0xF0,0x38,0x0E,0x03,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xF0,0x98,0x76,0x0E,0x05,0x07,0x01,0x00,
0x01,0x10,0x03,0x8E,0x30,0x60,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0xFF,0xDF,0xF0,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x02,0x06,0x0B,0x56,0x6F,0x38,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x01,0x07,0x00,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x01,0x01,0x03,0x02,0x06,0x0C,
0x08,0x38,0xB0,0x60,0xC0,0x80,0x00,0xC0,0x07,0x05,0x0A,0x1C,0x10,0x20,0x70,0x40,
0x80,0xA0,0x00,0x80,0x00,0x00,0x04,0x01,0x12,0x84,0xE8,0xF0,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x07,0x1F,0x1C,0x0F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x10,0x70,
0x50,0x40,0xC0,0x40,0x0C,0x7D,0xC4,0x08,0x08,0x08,0x08,0x00,0x08,0xFF,0x88,0x02,
0x0C,0x0C,0x08,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x01,0x01,0x03,0x07,0x0F,0x08,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,
0x00,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

/* 32×32 点阵字模 (128 字节/字, FreeType monochrome, 列优先) */
static const uint8_t glyph32_zhao[128] = 
{
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x80,0x00,0x04,0x02,0xe0,
    0x00,0x04,0x02,0x3e,0x00,0x04,0xe2,0x07,0x00,0x04,0xe2,0x08,0x00,0x04,0x22,0x10,
    0x00,0x04,0x02,0x30,0xe0,0xff,0xff,0x3f,0xc0,0xff,0xff,0x7f,0x00,0x04,0x46,0xc0,
    0x00,0x04,0x42,0xc0,0x00,0x04,0x42,0x80,0x00,0x06,0x42,0x80,0x00,0x06,0x63,0x80,
    0x00,0x04,0x43,0x90,0x00,0x00,0x02,0x88,0x00,0x02,0x00,0x86,0x00,0x04,0x00,0x83,
    0x00,0x18,0x80,0x81,0x00,0x60,0xe0,0x80,0x00,0xc0,0x38,0x80,0x00,0x80,0x1f,0x80,
    0x00,0x80,0x0f,0x80,0x00,0xe0,0x1d,0x80,0x00,0x7c,0x70,0x80,0x80,0x1f,0xe0,0x83,
    0x80,0x03,0x80,0x8f,0x00,0x01,0x00,0x86,0x00,0x00,0x00,0x80,0x00,0x00,0x00,0x00,
};
static const uint8_t glyph32_si[128] = 
{
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xe0,0xff,0xff,0x0f,0x20,0x00,0x40,0x01,
    0x20,0x00,0x20,0x01,0x20,0x00,0x20,0x01,0x20,0x00,0x18,0x01,0x20,0x00,0x06,0x01,
    0xe0,0xff,0x03,0x01,0xe0,0x3f,0x00,0x01,0x20,0x00,0x00,0x01,0x20,0x00,0x00,0x01,
    0x20,0x00,0x00,0x01,0x20,0x00,0x00,0x01,0xe0,0xff,0x0f,0x01,0xe0,0xff,0x0f,0x01,
    0x20,0x00,0x08,0x01,0x20,0x00,0x08,0x01,0x20,0x00,0x0c,0x01,0x20,0x00,0x0c,0x01,
    0x20,0x00,0x00,0x01,0xf0,0xff,0xff,0x0f,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

/* 用 32×32 字模在全屏绘制一个字符 (SSD1306 page 格式, 目标 x/y 为像素坐标) */
/**
 * @brief  在缓冲区绘制 32×32 点阵字模
 * @param  buf 参数说明
 * @param  glyph 参数说明
 * @param  dx 参数说明
 * @param  dy 参数说明
 * @date   2026-08-07
 */
static void draw_glyph32(uint8_t *buf, const uint8_t *glyph, int16_t dx, int16_t dy)
{
    /* glyph 128 字节: 32 列 × 4 组 8 行 (LSB=页顶), 列优先 */
    for (int16_t col = 0; col < 32; col++)
    {
        int16_t tx = dx + col;
        if (tx < 0 || tx >= 128)
        {
            continue;
        }
        for (int16_t rp = 0; rp < 4; rp++)
        {
            uint8_t byte_val = glyph[col * 4 + rp];
            int16_t base_y = dy + rp * 8;
            for (int16_t bit = 0; bit < 8; bit++)
            {
                if (byte_val & (1 << bit))
                {
                    int16_t ty = base_y + bit;
                    if (ty >= 0 && ty < 64)
                    {
                        buf[(ty / 8) * 128 + tx] |= (1 << (ty % 8));
                    }
                }
            }
        }
    }
}

/* 大号文字 "赵四" 32×32 真点阵, 全屏居中 */
/**
 * @brief  绘制大号文字「赵四」（32×32 真点阵，全屏居中）
 * @date   2026-08-07
 */
void draw_bigtext_zhaosi(void)
{
    uint8_t *buf = ssd1306_get_buffer();
    memset(buf, 0x00, 128 * 8);

    /* 两个汉字 × 32px = 64px, 居中: x=(128-64)/2=32, y=(64-32)/2=16 */
    const uint8_t *glyphs[2] = { glyph32_zhao, glyph32_si };
    int16_t x = 32;
    int16_t y = 16;

    for (int i = 0; i < 2; i++)
    {
        draw_glyph32(buf, glyphs[i], x, y);
        x += 32;
    }
}

/* PREVIEW 渲染回调：欢迎语 */
/**
 * @brief  上电文字预览渲染：欢迎语
 * @date   2026-08-07
 */
static void preview_render_welcome(void)
{
    const char *text = sys_config_get_boot_text();
    /* 复用 display_mgr 的全屏文字绘制（通过声明 extern 访问） */
    display_mgr_set_text(text ? text : "欢迎进入系统");
}

/* PREVIEW 渲染回调：Logo */
/**
 * @brief  上电文字预览渲染：Logo
 * @date   2026-08-07
 */
static void preview_render_logo(void)
{
    uint8_t *buf = ssd1306_get_buffer();
    memcpy(buf, logo_boot_bmp, 1024);
}

/* PREVIEW 渲染回调：大号文字 */
/**
 * @brief  上电文字预览渲染：大号文字
 * @date   2026-08-07
 */
static void preview_render_bigtext(void)
{
    draw_bigtext_zhaosi();
}

/* PREVIEW 确认回调 */
/**
 * @brief  上电文字确认回调：欢迎语
 * @date   2026-08-07
 */
static void preview_confirm_welcome(void)
{
    g_poweron_type = SYS_CONFIG_POWERON_WELCOME;

    storage_cmd_t cmd = {0};
    cmd.type = STORAGE_CMD_POWERON_TYPE;
    cmd.u.poweron_type = SYS_CONFIG_POWERON_WELCOME;
    (void)app_ipc_send_storage_cmd(&cmd, 0);
}

/**
 * @brief  上电文字确认回调：Logo
 * @date   2026-08-07
 */
static void preview_confirm_logo(void)
{
    g_poweron_type = SYS_CONFIG_POWERON_LOGO;

    storage_cmd_t cmd = {0};
    cmd.type = STORAGE_CMD_POWERON_TYPE;
    cmd.u.poweron_type = SYS_CONFIG_POWERON_LOGO;
    (void)app_ipc_send_storage_cmd(&cmd, 0);
}

/**
 * @brief  上电文字确认回调：大号文字
 * @date   2026-08-07
 */
static void preview_confirm_bigtext(void)
{
    g_poweron_type = SYS_CONFIG_POWERON_BIGTEXT;

    storage_cmd_t cmd = {0};
    cmd.type = STORAGE_CMD_POWERON_TYPE;
    cmd.u.poweron_type = SYS_CONFIG_POWERON_BIGTEXT;
    (void)app_ipc_send_storage_cmd(&cmd, 0);
}

/* --- 固件升级确认回调 --- */
/**
 * @brief  固件升级确认回调
 * @date   2026-08-11
 */
static void cb_update_firmware(void)
{
    app_fw_info_set_ota_request();
    sys_config_reset();
}

/* --- 系统重启确认回调 --- */
/**
 * @brief  重启确认回调
 * @date   2026-08-07
 */
static void cb_reboot_confirm(void)
{
    sys_config_reset();
}

/* ================================================================
 * 菜单项定义 (自底向上, const 存储于 Flash)
 * ================================================================ */

/* ---- 三级示例 (Level 2) ---- */
static const menu_item_t item_demo_a = 
{
    .text = "选项" "A",  /* "选项A" */
    .type = MENU_TYPE_ACTION,
    .action = cb_demo_a,
};
	
static const menu_item_t item_demo_b = 
{
    .text = "选项" "B",  /* "选项B" */
    .type = MENU_TYPE_ACTION,
    .action = cb_demo_b,
};
	
static const menu_item_t item_demo_c = 
{
    .text = "选项" "C",  /* "选项C" */
    .type = MENU_TYPE_ACTION,
    .action = cb_demo_c,
};

static const menu_item_t *menu_demo_items[] = 
{
    &item_demo_a, &item_demo_b, &item_demo_c,
};

/* ---- 预留 子菜单 (Level 1) ---- */
static const menu_item_t item_demo = 
{
    .text = "三级示例",  /* "三级示例" */
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_demo_items, .count = 3 },
};

/* ---- 系统信息 (Level 1) ---- */
static const menu_item_t item_fw_ver = 
{
    .text = "固件版本",  /* "固件版本" */
    .type = MENU_TYPE_INFO,
    .info = { .detail_text = FW_VERSION },
};
static const menu_item_t item_runtime = 
{
    .text = "编译时间",  /* "编译时间" */
    .type = MENU_TYPE_INFO,
    .info = { .detail_text = FW_BUILD_TIME },
};
static const menu_item_t item_author = 
{
    .text = "作者",  /* "作者" */
    .type = MENU_TYPE_INFO,
    .info = { .detail_text = FW_AUTHOR },
};
/* 关于本项目的描述文本 */
static const char about_text[] =
    "OLED显示控制终端\n"
    "平台:STM32F407\n"
    "\n"
    "功能:\n"
    "- 本地/远程显示切换\n"
    "- 时间/天气/日期显示\n"
    "- 多种显示特效\n"
    "- LED状态指示\n"
    "- 开机画面定制\n"
    "- 固件配置保存\n"
    "\n"
    "用途: 物联网信息面板\n"
    "\n"
    "作者: 赵四\n"
    "版本: V1.0.2\n"
    "编译时间: 2026.08.07 16:02\n"
    "邮箱:429511192@qq.com";

static const menu_item_t item_about = 
{
    .text = "关于",
    .type = MENU_TYPE_INFO,
    .info = { .detail_text = about_text },
};

/* ---- 上电文字 PREVIEW 项 (Level 1) ---- */
static const menu_item_t item_poweron_display_0 = 
{
    .text = "欢迎语",
    .type = MENU_TYPE_PREVIEW,
    .preview = { .render = preview_render_welcome, .on_confirm = preview_confirm_welcome },
};
static const menu_item_t item_poweron_display_1 = 
{
    .text = "Logo",
    .type = MENU_TYPE_PREVIEW,
    .preview = { .render = preview_render_logo, .on_confirm = preview_confirm_logo },
};
static const menu_item_t item_poweron_display_2 = 
{
    .text = "大号文字",
    .type = MENU_TYPE_PREVIEW,
    .preview = { .render = preview_render_bigtext, .on_confirm = preview_confirm_bigtext },
};

/* ---- LED 控制 (Level 1) ---- */
static const menu_item_t item_led_off = 
{
    .text = "关闭",    /* "关闭" */
    .type = MENU_TYPE_ACTION,
    .action = cb_led_off,
};
static const menu_item_t item_led_on = 
{
    .text = "常亮",    /* "常亮" */
    .type = MENU_TYPE_ACTION,
    .action = cb_led_on,
};
static const menu_item_t item_led_blink = 
{
    .text = "闪烁",    /* "闪烁" */
    .type = MENU_TYPE_ACTION,
    .action = cb_led_blink,
};

/* ---- 显示特效 (Level 1) ---- */
static const menu_item_t item_effect_static = 
{
    .text = "静态",    /* "静态" */
    .type = MENU_TYPE_ACTION,
    .action = cb_effect_static,
};
static const menu_item_t item_effect_scroll_l = 
{
    .text = "左滚",    /* "左滚" */
    .type = MENU_TYPE_ACTION,
    .action = cb_effect_scroll_l,
};
static const menu_item_t item_effect_scroll_r = 
{
    .text = "右滚",    /* "右滚" */
    .type = MENU_TYPE_ACTION,
    .action = cb_effect_scroll_r,
};
static const menu_item_t item_effect_scroll_u = 
{
    .text = "上滚",    /* "上滚" */
    .type = MENU_TYPE_ACTION,
    .action = cb_effect_scroll_u,
};
static const menu_item_t item_effect_scroll_d = 
{
    .text = "下滚",    /* "下滚" */
    .type = MENU_TYPE_ACTION,
    .action = cb_effect_scroll_d,
};
static const menu_item_t item_effect_flip = 
{
    .text = "翻页",    /* "翻页" */
    .type = MENU_TYPE_ACTION,
    .action = cb_effect_flip,
};
static const menu_item_t item_effect_fade = 
{
    .text = "淡入淡出",  /* "淡入淡出" */
    .type = MENU_TYPE_ACTION,
    .action = cb_effect_fade,
};

/* ---- 显示内容 (Level 1) ---- */
static const menu_item_t item_content_time = 
{
    .text = "时间",    /* "时间" */
    .type = MENU_TYPE_ACTION,
    .action = cb_disp_time,
};
static const menu_item_t item_content_weather = 
{
    .text = "天气",    /* "天气" */
    .type = MENU_TYPE_ACTION,
    .action = cb_disp_weather,
};
static const menu_item_t item_content_date = 
{
    .text = "日期",    /* "日期" */
    .type = MENU_TYPE_ACTION,
    .action = cb_disp_date,
};
static const menu_item_t item_content_custom = 
{
    .text = "自定义文字",  /* "自定义文字" */
    .type = MENU_TYPE_ACTION,
    .action = cb_disp_custom,
};

/* ---- 工作模式 TOGGLE (Level 1) ---- */
static const menu_item_t item_mode_local = 
{
    .text = "本地",    /* "本地" */
    .type = MENU_TYPE_TOGGLE,
    .toggle = { .value_ptr = &g_remote_mode, .checked_value = 0, .on_change = cb_mode_changed },
};
static const menu_item_t item_mode_remote = 
{
    .text = "远程",    /* "远程" */
    .type = MENU_TYPE_TOGGLE,
    .toggle = { .value_ptr = &g_remote_mode, .checked_value = 1, .on_change = cb_mode_changed },
};

/* ---- 对比度 VALUE 项 (Level 2, 位于"对比度设置"子菜单下) ---- */
static const menu_item_t item_contrast = 
{
    .text = "对比度",
    .type = MENU_TYPE_VALUE,
    .value = { .value_ptr = &g_contrast, .min = 0, .max = 255, .step = 5,
               .on_change = cb_contrast_changed },
};

/* ---- 对比度设置 三级菜单 (Level 2) ---- */
static const menu_item_t *menu_contrast_items[] = 
{
    &item_contrast,
};

/* ---- 对比度设置 二级菜单 (Level 1, 位于"设置"下) ---- */
static const menu_item_t item_contrast_menu = 
{
    .text = "对比度设置",
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_contrast_items, .count = 1 },
};

/* --- 固件升级 CONFIRM 项 (Level 2, 位于"设置"子菜单下) --- */
static const menu_item_t item_update_firmware =
{
    .text = "升级固件",
    .type = MENU_TYPE_CONFIRM,
    .confirm = { .prompt_text = "确认进入升级？",
                 .on_confirm = cb_update_firmware },
};

/* --- 重启 CONFIRM 项 (Level 2, 位于"设置"子菜单下) --- */
static const menu_item_t item_reboot = 
{
    .text = "重启",
    .type = MENU_TYPE_CONFIRM,
    .confirm = { .prompt_text = "真的要重启吗？",
                 .on_confirm = cb_reboot_confirm },
};

/* ---- 设置 二级菜单 (Level 1) ---- */
static const menu_item_t *menu_setting_items[] = 
{
    &item_contrast_menu,
    &item_update_firmware,
    &item_reboot,
};

/* ================================================================
 * 子菜单数组 (Level 1 各组)
 * ================================================================ */

static const menu_item_t *menu_mode_items[] = 
{
    &item_mode_local, &item_mode_remote,
};

static const menu_item_t *menu_content_items[] = 
{
    &item_content_time, &item_content_weather,
    &item_content_date, &item_content_custom,
};

static const menu_item_t *menu_effect_items[] = 
{
    &item_effect_static, &item_effect_scroll_l, &item_effect_scroll_r,
    &item_effect_scroll_u, &item_effect_scroll_d,
    &item_effect_flip, &item_effect_fade,
};

static const menu_item_t *menu_led_items[] = 
{
    &item_led_off, &item_led_on, &item_led_blink,
};

static const menu_item_t *menu_poweron_display_items[] = 
{
    &item_poweron_display_0, &item_poweron_display_1, &item_poweron_display_2,
};

static const menu_item_t *menu_sysinfo_items[] = 
{
    &item_fw_ver, &item_runtime, &item_author, &item_about,
};

static const menu_item_t *menu_reserved_items[] = 
{
    &item_demo,
};

/* ================================================================
 * 主菜单 (Level 0) — 8 个顶层项
 * ================================================================ */

static const menu_item_t item_main_1 = 
{
    .text = "1.工作模式",  /* "1.工作模式" */
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_mode_items, .count = 2 },
};
static const menu_item_t item_main_2 = 
{
    .text = "2.显示内容",  /* "2.显示内容" */
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_content_items, .count = 4 },
};
static const menu_item_t item_main_3 = 
{
    .text = "3.显示特效",  /* "3.显示特效" */
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_effect_items, .count = 7 },
};

static const menu_item_t item_main_4 = 
{
    .text = "4.设置",/* "4.设置" */
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_setting_items, .count = 3 },
};

static const menu_item_t item_main_5 = 
{
    .text = "5.LED控制",  /* "5.LED控制" */
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_led_items, .count = 3 },
};

static const menu_item_t item_main_6 = 
{
    .text = "6.上电画面",  /* "6.上电画面" */
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_poweron_display_items, .count = 3 },
};

static const menu_item_t item_main_7 = 
{
    .text = "7.系统信息",  /* "7.系统信息" */
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_sysinfo_items, .count = 4 },
};
static const menu_item_t item_main_8 = 
{
    .text = "8.预留",  /* "8.预留" */
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_reserved_items, .count = 1 },
};

static const menu_item_t *menu_main_items[] = 
{
    &item_main_1, &item_main_2, &item_main_3,
    &item_main_4, &item_main_5, &item_main_6,
    &item_main_7, &item_main_8,
};

/* ================================================================
 * 根节点 (虚拟 SUBMENU, 含 8 个主菜单项)
 * ================================================================ */

static const menu_item_t menu_root = 
{
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
    g_poweron_type = sys_config_get_poweron_type();
    /* g_contrast 保持当前值不变，不硬编码重置 */
}
