/**
 * @file    freertos_app.c
 * @brief   FreeRTOS 应用任务集成
 */

#include "FreeRTOS.h"
#include "task.h"
#include "freertos_app.h"
#include "user_app.h"

#define APP_TASK_STACK_WORDS      1024U
#define APP_TASK_PRIORITY         (tskIDLE_PRIORITY + 1U)

static TaskHandle_t app_task_handle = NULL;

/**
 * @brief  APP 主任务，承载原有 user_app_handle 逻辑
 * @param  argument 任务参数，本任务不使用
 * @return 无
 */
static void app_task(void *argument)
{
    (void)argument;

    for (;;)
    {
        user_app_handle();
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
}

/**
 * @brief  创建 APP 主任务
 * @param  无
 * @return 无
 */
void freertos_app_init(void)
{
    BaseType_t ret = pdFAIL;

    ret = xTaskCreate(app_task,
                      "app_task",
                      APP_TASK_STACK_WORDS,
                      NULL,
                      APP_TASK_PRIORITY,
                      &app_task_handle);

    configASSERT(ret == pdPASS);
}

/**
 * @brief  栈溢出 Hook，configCHECK_FOR_STACK_OVERFLOW=2 时由内核调用
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}

/**
 * @brief  堆分配失败 Hook，configUSE_MALLOC_FAILED_HOOK=1 时由内核调用
 */
void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}

