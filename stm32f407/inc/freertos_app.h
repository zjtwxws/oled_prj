/**
 * @file    freertos_app.h
 * @brief   FreeRTOS 应用任务集成接口
 */

#ifndef FREERTOS_APP_H
#define FREERTOS_APP_H

/**
 * @brief  创建 FreeRTOS 应用任务
 * @param  无
 * @return 无
 */
void freertos_app_init(void);

/**
 * @brief  开启任务调度
 * @param  无
 * @return 无
 */
void freertos_task_start(void);

#endif

