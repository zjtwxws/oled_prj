/**
 * @file    display_mgr.h
 * @brief   显示管理器 — 本地/远程双模式 + 7种特效调度 (v3.1)
 *
 * 本地模式: STM32 全屏渲染文字+7种特效 (128×64, 无状态栏)
 * 远程模式: 接收 PC 帧缓冲分段→拼装→刷屏 (STM32 不自行渲染)
 */

#ifndef __DISPLAY_MGR_H
#define __DISPLAY_MGR_H

#include <stdint.h>
#include <stdbool.h>

/* 远程子模式枚举 */
typedef enum {
    REMOTE_SUB_TEXT    = 0,  /* 文字 (支持7种特效,PC端渲染) */
    REMOTE_SUB_TIME    = 1,  /* 时钟 (静态,PC端渲染) */
    REMOTE_SUB_WEATHER = 2,  /* 天气 (静态,PC端渲染) */
    REMOTE_SUB_DATE    = 3,  /* 日期 (静态,PC端渲染) */
} remote_sub_mode_t;

/* 显示特效枚举 (本地模式+远程文字子模式) */
typedef enum {
    DISP_MODE_STATIC   = 0,    /* 静态显示 */
    DISP_MODE_SCROLL_L = 1,   /* 左滚 */
    DISP_MODE_SCROLL_R = 2,   /* 右滚 */
    DISP_MODE_SCROLL_U = 3,   /* 上滚 */
    DISP_MODE_SCROLL_D = 4,   /* 下滚 */
    DISP_MODE_FLIP     = 5,   /* 翻页 */
    DISP_MODE_FADE     = 6,   /* 淡入淡出 */
    DISP_MODE_COUNT           /* 模式总数 */
} display_mode_t;

/* 时间/天气信息 (仅存储, 本地+远程均不绘制状态栏) */
typedef struct {
    uint8_t  weather_type;   /* 天气类型 (0=晴,1=多云,2=雨,3=雪,4=风...由PC端定义) */
    int8_t   temperature;    /* 温度 (-128~127, 摄氏度) */
    uint8_t  humidity;      /* 湿度 (0~100%) */
    uint8_t  wind_dir;       /* 风向 (0~7, 0=北,1=东北,...由PC端定义) */
    uint8_t  year, month, day;  /* 日期: 年(0~99, 2000+year), 月(1~12), 日(1~31) */
    uint8_t  hour, minute, second;  /* 时间: 时(0~23), 分(0~59), 秒(0~59) */
    uint8_t  week_day;      /* 星期 (0=日,1~6=一~六) */
    uint8_t  led_state;      /* LED 状态 (0=关,1=开,2=闪烁) */
} display_status_t;

/* ---- 初始化 ---- */
void display_mgr_init(const char *boot_text);

/* ---- 本地/远程模式切换 ---- */
void display_mgr_set_remote(bool remote);
bool display_mgr_is_remote(void);

/* ---- 远程子模式 ---- */
void display_mgr_set_sub_mode(uint8_t sub_mode);
uint8_t display_mgr_get_sub_mode(void);

/* ---- 远程帧缓冲接收 ---- */
void display_mgr_rx_frame_seg(uint8_t seg, uint8_t total, const uint8_t *data, uint8_t len);

/* ---- 本地模式: 文字+特效 ---- */
void display_mgr_set_text(const char *text);
void display_mgr_set_boot_text(const char *text);
const char* display_mgr_get_boot_text(void);
void display_mgr_set_mode(display_mode_t mode);
display_mode_t display_mgr_get_mode(void);
void display_mgr_next_mode(void);

/* ---- 时间/天气数据存储 (不影响渲染) ---- */
void display_mgr_update_status(const display_status_t *status);
const display_status_t* display_mgr_get_status(void);

/* ---- 远程模式串口断开提示 ---- */
void display_mgr_show_disconnect(void);
void display_mgr_hide_disconnect(void);

/* ---- 主 tick (双模式调度) ---- */
void display_mgr_tick(void);

/* ---- 菜单退出后恢复显示 ---- */
void display_mgr_redraw(void);
/** @brief 菜单激活期间抑制 display_mgr 自行刷屏，防止与 menu_mgr 冲突 */
void display_mgr_set_menu_suppress(bool suppress);

#endif
