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

/* --- 初始化 --- */

/**
 * @brief 显示管理器初始化
 * @param boot_text  上电默认文字 (UTF-8 编码)
 */
void display_mgr_init(const char *boot_text);

/* --- 内容设置 --- */

/**
 * @brief 设置主内容区显示文字
 * @param text  UTF-8 编码字符串
 */
void display_mgr_set_text(const char *text);

/**
 * @brief 设置上电默认文字
 */
void display_mgr_set_boot_text(const char *text);

/**
 * @brief 获取上电默认文字
 */
const char* display_mgr_get_boot_text(void);

/* --- 显示模式 --- */

/**
 * @brief 设置显示模式
 */
void display_mgr_set_mode(display_mode_t mode);

/**
 * @brief 获取当前显示模式
 */
display_mode_t display_mgr_get_mode(void);

/**
 * @brief 切换到下一个显示模式 (按键循环)
 */
void display_mgr_next_mode(void);

/* --- 状态更新 --- */

/**
 * @brief 更新状态栏信息 (时间/天气/LED)
 */
void display_mgr_update_status(const display_status_t *status);

/**
 * @brief 获取当前状态指针 (供 UI 同步用)
 */
const display_status_t* display_mgr_get_status(void);

/* --- 周期任务 --- */

/**
 * @brief 显示管理器 tick (需在主循环或定时器中每 20~50ms 调用一次)
 *        用于驱动滚动、翻页、淡入淡出等动画
 */
void display_mgr_tick(void);

#endif /* __DISPLAY_MGR_H */
