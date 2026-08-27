/**
 * @file    freertos_app.c
 * @brief   FreeRTOS 应用任务集成
 */

#include "FreeRTOS.h"
#include "task.h"
#include "freertos_app.h"
#include "user_app.h"
#include "debug_console.h"
#include "cli_cmds.h"

#define APP_TASK_STACK_WORDS      1024U
#define APP_TASK_PRIORITY         (tskIDLE_PRIORITY + 1U)

#define CLI_TASK_STACK_WORDS      256U
#define CLI_TASK_PRIORITY         (tskIDLE_PRIORITY + 2U)

static TaskHandle_t app_task_handle = NULL;
static TaskHandle_t cli_task_handle = NULL;

/**
 * @brief  APP 主任务，承载原有 user_app_handle 逻辑
 * @param  argument 任务参数，本任务不使用
 * @return 无
 */
static void app_task(void *argument)
{
    (void)argument;
	DEBUG_PRINTF("app_task start");

    for (;;)
    {
        user_app_handle();
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
}

/**
 *  @fn     cli_task
 *  @brief  命令行任务，主要用于命令行的调度和处理
 *  @param  argument 任务参数，本任务不使用
 *  @return 无
 *  @author zhaojiantao
 *  @date   2026/8/26
 *  @note   none
 */
void cli_task(void *argument)
{
	(void)argument;
	DEBUG_PRINTF("cli_task start");
	
	for(;;)
	{
		cli_poll();
		vTaskDelay(pdMS_TO_TICKS(1U));
	}
}
/**
 * @brief  创建任务
 * @param  无
 * @return 无
 */
void tasks_init(void)
{
    BaseType_t ret = pdFAIL;

    ret = xTaskCreate(app_task,
                      "app_task",
                      APP_TASK_STACK_WORDS,
                      NULL,
                      APP_TASK_PRIORITY,
                      &app_task_handle);

    configASSERT(ret == pdPASS);

	ret = xTaskCreate(cli_task,
					  "cli_task",
					  CLI_TASK_STACK_WORDS,
					  NULL,
					  CLI_TASK_PRIORITY,
					  &cli_task_handle);
	
	configASSERT(ret == pdPASS);
}

/**
 * @brief  cli_tasks_info — 显示所有任务的状态、优先级与剩余栈空间
 * @param  argc  参数个数
 * @param  argv  参数数组
 * @return 0=成功
 * @note   剩余栈空间单位为字节，为任务创建以来的历史最低剩余值，越小越接近栈溢出
 */
static int cli_tasks_info(uint8_t argc, char **argv)
{
    (void)argc;
    (void)argv;

    static TaskStatus_t status[16];
    UBaseType_t task_count = uxTaskGetNumberOfTasks();

    DEBUG_PRINTF("Total tasks: %lu", (unsigned long)task_count);
    DEBUG_PRINTF("%-12s %-5s %-4s %-10s", "Name", "State", "Prio", "Free(W)");

    UBaseType_t n = uxTaskGetSystemState(status, 16, NULL);
    for (UBaseType_t i = 0; i < n; i++)
    {
        const char *state_str;
        switch (status[i].eCurrentState)
        {
        case eRunning:
            state_str = "RUN";
            break;
        case eReady:
            state_str = "RDY";
            break;
        case eBlocked:
            state_str = "BLK";
            break;
        case eSuspended:
            state_str = "SUS";
            break;
        case eDeleted:
            state_str = "DEL";
            break;
        default:
            state_str = "INV";
            break;
        }

        DEBUG_PRINTF("%-12s %-5s %-4lu %-10lu",
                     status[i].pcTaskName,
                     state_str,
                     (unsigned long)status[i].uxCurrentPriority,
                     (unsigned long)status[i].usStackHighWaterMark);
    }

    return 0;
}

/**
 * @brief  创建 APP 主任务
 * @param  无
 * @return 无
 */
void freertos_app_init(void)
{
	tasks_init();
	cli_cmd_register("tasks_info", cli_tasks_info, "显示任务状态/优先级/栈剩余空间");
}

/**
 * @brief  开启任务调度
 * @param  无
 * @return 无
 */
void freertos_task_start(void)
{
	vTaskStartScheduler();
	return;
}

/**
 * @brief  栈溢出 Hook，configCHECK_FOR_STACK_OVERFLOW=2 时由内核调用
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    DEBUG_PRINTF("task[%s]  stack overflow", pcTaskName);
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

