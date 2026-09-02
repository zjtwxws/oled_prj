/**
 * @file    watchdog_monitor.h
 * @brief   任务看门狗监视头文件
 */

#ifndef __WATCHDOG_MONITOR_H
#define __WATCHDOG_MONITOR_H

/* 注册当前任务，返回 slot ID；失败返回 -1 */
int wdt_monitor_register(const char *name, uint32_t timeout_ms);

/* 任务报到（在任务主循环内周期调用）*/
void wdt_monitor_feed(int slot_id);

/* 监视器主循环（在 WatchdogTask 内调用）*/
void wdt_monitor_check(void);

#endif

