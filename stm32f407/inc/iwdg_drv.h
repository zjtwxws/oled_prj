/**
 * @file    iwdg_drv.h
 * @brief   独立看门狗驱动 (通过 IWDG_ENABLE 宏控制开关)
 */

#ifndef __IWDG_DRV_H
#define __IWDG_DRV_H

/*
 * 调试开关: 注释掉下行以关闭看门狗
 * 发布时取消注释
 */
#define IWDG_ENABLE  1

#ifdef IWDG_ENABLE

void iwdg_drv_init(void);   /* 初始化 IWDG, 复位周期约 1s */
void iwdg_drv_feed(void);   /* 喂狗 */

#else

/* 空实现 (调试模式) */
#define iwdg_drv_init()   ((void)0)   /* 看门狗关闭时的空实现 */
#define iwdg_drv_feed()   ((void)0)   /* 看门狗关闭时的空实现 */

#endif /* IWDG_ENABLE */

#endif /* __IWDG_DRV_H */
