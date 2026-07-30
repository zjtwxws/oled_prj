/**
 * @file    display_mgr.h
 * @brief   显示管理器 — OLED 区域划分 + 7种特效调度
 */

#ifndef __DISPLAY_MGR_H
#define __DISPLAY_MGR_H

#include <stdint.h>

/* 显示区域定义 */
#define DISP_STATUS_BAR_H   16      /* 状态栏高度(像素) */
#define DISP_CONTENT_Y      DISP_STATUS_BAR_H
#define DISP_CONTENT_H      (64 - DISP_STATUS_BAR_H)  /* 内容区 48px */

/* 显示模式枚举 */
typedef enum {
    DISP_MODE_STATIC   = 0,
    DISP_MODE_SCROLL_L = 1,   /* 左滚 */
    DISP_MODE_SCROLL_R = 2,   /* 右滚 */
    DISP_MODE_SCROLL_U = 3,   /* 上滚 */
    DISP_MODE_SCROLL_D = 4,   /* 下滚 */
    DISP_MODE_FLIP     = 5,   /* 翻页 */
    DISP_MODE_FADE     = 6,   /* 淡入淡出 */
    DISP_MODE_COUNT           /* 模式总数 */
} display_mode_t;

/* 天气 & 时间信息 (用于状态栏绘制) */
typedef struct {
    uint8_t  weather_type;     /* 0=晴...6=雪 */
    int8_t   temperature;      /* ℃ */
    uint8_t  humidity;         /* % */
    uint8_t  wind_dir;         /* 0=北...7=西北 */
    uint8_t  year, month, day;
    uint8_t  hour, minute, second;
    uint8_t  week_day;         /* 0=日 */
    uint8_t  led_state;        /* 0=关,1=开,2=闪烁 */
} display_status_t;

void display_mgr_init(const char *boot_text);
void display_mgr_set_text(const char *text);
void display_mgr_set_boot_text(const char *text);
const char* display_mgr_get_boot_text(void);
void display_mgr_set_mode(display_mode_t mode);
display_mode_t display_mgr_get_mode(void);
void display_mgr_next_mode(void);
void display_mgr_update_status(const display_status_t *status);
const display_status_t* display_mgr_get_status(void);
void display_mgr_tick(void);
void display_mgr_sync_frame(void);

#endif
