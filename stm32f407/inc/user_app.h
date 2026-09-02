/**
 * @file    user_app.h
 * @brief   用户程序的头文件接口
 */

#ifndef __USER_APP_H
#define __USER_APP_H

#include "app_ipc.h"

/* APP Slot 基地址 — 编译宏 APP_SLOT_B/APP_SLOT_A 控制 */
#ifndef APP_VTOR_ADDR
#if defined(APP_SLOT_B)
#define APP_VTOR_ADDR  0x08060000UL     /* Slot B */
#elif defined(APP_SLOT_A)
#define APP_VTOR_ADDR  0x08008000UL     /* Slot A (bootloader 下运行) */
#else
#define APP_VTOR_ADDR  0x08000000UL     /* 独立运行 (无 bootloader) */
#endif
#endif /* APP_VTOR_ADDR */
/** 固件版本号 */
#define FW_VERSION  "V1.5.0"
#define FW_AUTHOR  "赵四"
#define FW_BUILD_TIME  "2026.09.02 11:01"
#define FW_EMAIL  "429511192@qq.com"
#define FW_RTOS_VERN "freertos 11.3.0"

/** @brief 用户应用初始化 (外设+驱动+显示+菜单)，在 main() 中调用一次 */
int user_app_init(void);
/** @brief 串口任务处理 (协议接收、断线检测、统一 UART1 发送) */
void user_app_comm_process(void);
/** @brief 按键任务处理 (扫描与分发) */
void user_app_key_process(void);
/** @brief 串口任务统一发送协议上行帧 */
void user_app_send_proto_tx(const proto_tx_req_t *req);

#endif
