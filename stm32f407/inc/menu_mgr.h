/**
 * @file    menu_mgr.h
 * @brief   多级菜单管理器 — 导航状态机 + 渲染
 *
 * 支持 5 种菜单项: SUBMENU / TOGGLE / VALUE / ACTION / INFO
 * OLED 128×64, 4 行 × 16px 布局, 中英文混排
 */

#ifndef __MENU_MGR_H
#define __MENU_MGR_H

#include <stdint.h>
#include <stdbool.h>
#include "key_drv.h"

#define MENU_MAX_DEPTH  8      /* 菜单最大嵌套深度 (含根菜单) */

#if defined(__ARMCC_VERSION) && (__ARMCC_VERSION < 6000000)
#pragma push
#pragma anon_unions
#endif

/* 菜单项类型 */
typedef enum {
    MENU_TYPE_SUBMENU,      /* 进入子菜单 */
    MENU_TYPE_TOGGLE,       /* ON/OFF 开关, 按确认切换 */
    MENU_TYPE_VALUE,        /* 数值调节, KEY3 进入编辑, KEY1/KEY2 增减 */
    MENU_TYPE_ACTION,       /* 执行回调函数 */
    MENU_TYPE_INFO,         /* 纯信息显示 (如版本号), 按确认/返回退出 */
    MENU_TYPE_PREVIEW,      /* 全屏预览: KEY3进入预览→KEY3确认选定退出/KEY4取消退出 */
    MENU_TYPE_CONFIRM,      /* 确认对话框: 显示提示文字, KEY3执行确认回调, KEY4取消 */
} menu_item_type_t;

/* 菜单项定义 (存储于 Flash, const) */
typedef struct menu_item {
    const char            *text;          /* 显示文本 (UTF-8) */
    menu_item_type_t       type;          /* 菜单项类型 */
    union {
        /* SUBMENU: 子菜单数组 */
        struct {
            const struct menu_item **items;
            uint8_t                  count;
        } submenu;

        /* TOGGLE: 指向 bool (0/1), 按确认翻转 */
        struct {
            uint8_t *value_ptr;             /* 指向实际状态变量的指针 */
            uint8_t  checked_value;         /* *value_ptr == checked_value 时显示 [x] */
            void   (*on_change)(uint8_t);   /* 状态变化回调 (可选) */
        } toggle;

        /* VALUE: 数值范围调节 */
        struct {
            uint8_t *value_ptr;             /* 指向实际值变量的指针 */
            uint8_t  min, max;              /* 调节范围 */
            uint8_t  step;                  /* 每次调节步长 */
            void   (*on_change)(uint8_t);   /* 值变化回调 (可选) */
        } value;

        /* ACTION: 执行回调 */
        void (*action)(void);

        /* INFO: 纯信息显示 */
        struct {
            const char *detail_text;        /* 详细信息文本 */
        } info;

        /* PREVIEW: 全屏预览 + 确认/取消 */
        struct {
            void (*render)(void);           /* 全屏渲染回调 (由 menu_items.c 实现) */
            void (*on_confirm)(void);       /* KEY3 确认时调用 (保存选择,可选) */
        } preview;

        /* CONFIRM: 确认对话框, KEY3 执行 on_confirm, KEY4 取消 */
        struct {
            const char *prompt_text;        /* 提示文字, 如 "真的要重启吗？" */
            void      (*on_confirm)(void); /* KEY3 确认时执行的回调 (如系统复位) */
        } confirm;
    };
} menu_item_t;

/* 初始化菜单系统 (构建菜单树, 进入主菜单) */
void menu_mgr_init(void);

/* 处理按键事件 (由 user_app 按键分发调用) */
void menu_mgr_handle_key(uint8_t key_id, key_event_t event);

/* 每帧渲染 (由 user_app 主循环调用, ~50ms 周期) */
void menu_mgr_tick(void);

/* 查询/切换菜单激活状态 */
bool menu_mgr_is_active(void);
void menu_mgr_activate(void);
void menu_mgr_deactivate(void);

/* 检查菜单是否发生变化 (优化: 仅变化时刷新 OLED) */
bool menu_mgr_is_dirty(void);

/* 菜单项定义查询接口 (menu_items.c) */
const struct menu_item* menu_items_get_root(void);
void menu_items_init_state(void);


#if defined(__ARMCC_VERSION) && (__ARMCC_VERSION < 6000000)
#pragma pop
#endif

#endif
 /* __MENU_MGR_H */
 
 
