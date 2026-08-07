/**
 * @file    user_app.h
 * @brief   用户程序的头文件接口
 */

#ifndef __USER_APP_H
#define __USER_APP_H
/** 固件版本号 */
#define FW_VERSION  "V1.0.3"
#define FW_AUTHOR  "赵四"
#define FW_BUILD_TIME  "2026.08.07 16:02"
#define FW_EMAIL  "429511192@qq.com"

/** @brief 用户应用初始化 (外设+驱动+显示+菜单)，在 main() 中调用一次 */
int user_app_init(void);
/** @brief 用户应用主循环 (协议处理+按键扫描+LED/显示 tick+喂狗)，在 while(1) 中每帧调用 */
int user_app_handle(void);

#endif
