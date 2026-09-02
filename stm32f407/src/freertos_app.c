/**
 * @file    freertos_app.c
 * @brief   FreeRTOS 静态任务与任务循环实现
 */

#include "FreeRTOS.h"
#include "task.h"

#include "freertos_app.h"
#include "app_ipc.h"
#include "user_app.h"
#include "uart_drv.h"
#include "debug_console.h"
#include "cli_cmds.h"
#include "display_mgr.h"
#include "menu_mgr.h"
#include "led_mgr.h"
#include "sys_config.h"
#include "ssd1306.h"
#include "iwdg.h"
#include "app_watchdog.h"

#define COMM_TASK_STACK_WORDS       1024U
#define KEY_TASK_STACK_WORDS        512U
#define DISPLAY_TASK_STACK_WORDS    1024U
#define CLI_TASK_STACK_WORDS        512U
#define LED_TASK_STACK_WORDS        256U
#define STORAGE_TASK_STACK_WORDS    512U
#define WATCHDOG_TASK_STACK_WORDS   256U

#define COMM_TASK_PRIORITY      (tskIDLE_PRIORITY + 5U)
#define KEY_TASK_PRIORITY       (tskIDLE_PRIORITY + 4U)
#define DISPLAY_TASK_PRIORITY   (tskIDLE_PRIORITY + 3U)
#define CLI_TASK_PRIORITY       (tskIDLE_PRIORITY + 2U)
#define LED_TASK_PRIORITY       (tskIDLE_PRIORITY + 1U)
#define STORAGE_TASK_PRIORITY   (tskIDLE_PRIORITY + 1U)
#define WATCHDOG_TASK_PRIORITY  tskIDLE_PRIORITY

static StackType_t comm_stack[COMM_TASK_STACK_WORDS];
static StackType_t key_stack[KEY_TASK_STACK_WORDS];
static StackType_t display_stack[DISPLAY_TASK_STACK_WORDS];
static StackType_t cli_stack[CLI_TASK_STACK_WORDS];
static StackType_t led_stack[LED_TASK_STACK_WORDS];
static StackType_t storage_stack[STORAGE_TASK_STACK_WORDS];
static StackType_t watchdog_stack[WATCHDOG_TASK_STACK_WORDS];

static StaticTask_t comm_tcb;
static StaticTask_t key_tcb;
static StaticTask_t display_tcb;
static StaticTask_t cli_tcb;
static StaticTask_t led_tcb;
static StaticTask_t storage_tcb;
static StaticTask_t watchdog_tcb;

static TaskHandle_t comm_task_handle = NULL;
static TaskHandle_t key_task_handle = NULL;
static TaskHandle_t display_task_handle = NULL;
static TaskHandle_t cli_task_handle = NULL;
static TaskHandle_t led_task_handle = NULL;
static TaskHandle_t storage_task_handle = NULL;
static TaskHandle_t watchdog_task_handle = NULL;

/**
 * @brief  应用显示命令
 * @param  cmd 显示命令
 * @return 应用结果
 */
static apply_status_t apply_disp_cmd(const disp_cmd_t *cmd)
{
    switch (cmd->type)
    {
    case DISP_CMD_SET_REMOTE:
        display_mgr_set_remote(cmd->u.remote);
        break;

    case DISP_CMD_SET_REMOTE_SUB:
        if (cmd->u.remote_sub.remote &&
            cmd->u.remote_sub.sub_mode > REMOTE_SUB_DATE)
        {
            return APPLY_PARAM_ERROR;
        }
        display_mgr_set_remote(cmd->u.remote_sub.remote);
        display_mgr_set_sub_mode(cmd->u.remote_sub.sub_mode);
        break;

    case DISP_CMD_SET_MODE:
        if (cmd->u.mode >= DISP_MODE_COUNT)
        {
            return APPLY_PARAM_ERROR;
        }
        display_mgr_set_mode(cmd->u.mode);
        break;

    case DISP_CMD_NEXT_MODE:
        display_mgr_next_mode();
        break;

    case DISP_CMD_SET_SUB_MODE:
        if (cmd->u.sub_mode > REMOTE_SUB_DATE)
        {
            return APPLY_PARAM_ERROR;
        }
        display_mgr_set_sub_mode(cmd->u.sub_mode);
        break;

    case DISP_CMD_SET_TEXT:
        display_mgr_set_text(cmd->u.text);
        break;

    case DISP_CMD_SET_BOOT_TEXT:
        display_mgr_set_boot_text(cmd->u.boot_text);
        break;

    case DISP_CMD_UPDATE_STATUS:
        display_mgr_update_status(&cmd->u.status);
        break;

    case DISP_CMD_FRAME_SEG:
        if (!display_mgr_rx_frame_seg(cmd->u.frame_seg.seg,
                                      cmd->u.frame_seg.total,
                                      cmd->u.frame_seg.data,
                                      cmd->u.frame_seg.len))
        {
            return APPLY_PARAM_ERROR;
        }
        break;

    case DISP_CMD_SHOW_DISCONNECT:
        display_mgr_show_disconnect();
        break;

    case DISP_CMD_HIDE_DISCONNECT:
        display_mgr_hide_disconnect();
        break;

    case DISP_CMD_REDRAW:
        display_mgr_redraw();
        break;

    case DISP_CMD_SET_CONTRAST:
        ssd1306_set_contrast(cmd->u.contrast);
        break;

    default:
        return APPLY_PARAM_ERROR;
    }

    return APPLY_OK;
}

/**
 * @brief  应用 LED 命令
 * @param  cmd LED 命令
 * @return 应用结果
 */
static apply_status_t apply_led_cmd(const led_cmd_t *cmd)
{
    if (cmd->type == LED_CMD_SET_STATE)
    {
        if (cmd->state > LED_STATE_BLINK)
        {
            return APPLY_PARAM_ERROR;
        }
        led_mgr_set_state(cmd->state);
        return APPLY_OK;
    }

    if (cmd->type == LED_CMD_NEXT_MODE)
    {
        uint8_t next = (led_mgr_get_state() + 1U) % 3U;
        led_mgr_set_state((led_state_t)next);
        return APPLY_OK;
    }

    return APPLY_PARAM_ERROR;
}

/**
 * @brief  应用存储命令
 * @param  cmd 存储命令
 * @return 应用结果
 */
static apply_status_t apply_storage_cmd(const storage_cmd_t *cmd)
{
    int ret = -1;

    switch (cmd->type)
    {
    case STORAGE_CMD_BOOT_TEXT:
        ret = sys_config_set_boot_text(cmd->u.text);
        break;

    case STORAGE_CMD_POWERON_TYPE:
        ret = sys_config_set_poweron_type(cmd->u.poweron_type);
        break;

    default:
        return APPLY_PARAM_ERROR;
    }

    return (ret == 0) ? APPLY_OK : APPLY_FLASH_ERROR;
}

/**
 * @brief  串口协议任务
 * @param  argument 任务参数
 */
static void comm_task(void *argument)
{
    (void)argument;
    DEBUG_PRINTF("comm_task start");

    for (;;)
    {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2U));

        if (uart_drv_available() > 0U)
        {
            user_app_comm_process();
        }

        proto_tx_req_t tx;
        while (xQueueReceive(g_proto_tx_queue, &tx, 0) == pdPASS)
        {
            user_app_send_proto_tx(&tx);
        }
    }
}

/**
 * @brief  按键任务
 * @param  argument 任务参数
 */
static void key_task(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    DEBUG_PRINTF("key_task start");

    for (;;)
    {
        user_app_key_process();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(20U));
    }
}

/**
 * @brief  显示任务
 * @param  argument 任务参数
 */
static void display_task(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    DEBUG_PRINTF("display_task start");

    for (;;)
    {
        disp_cmd_t cmd;
        while (xQueueReceive(g_disp_cmd_queue, &cmd, 0) == pdPASS)
        {
            apply_status_t status = apply_disp_cmd(&cmd);

            if (cmd.report_status && status == APPLY_OK)
            {
                proto_tx_req_t tx = {0};
                tx.type = PROTO_TX_MODE_STATUS;
                (void)app_ipc_send_proto_tx(&tx, pdMS_TO_TICKS(10U));
            }

            if (cmd.need_result)
            {
                cmd_result_t result =
                {
                    .seq = cmd.seq,
                    .cmd = cmd.proto_cmd,
                    .status = status,
                };
                (void)app_ipc_send_apply_result(&result, pdMS_TO_TICKS(10U));
            }
        }

        if (menu_mgr_is_active())
        {
            display_mgr_set_menu_suppress(true);
            menu_mgr_tick();
        }
        else
        {
            display_mgr_set_menu_suppress(false);
            display_mgr_tick();
            display_mgr_flush();
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(50U));
    }
}

/**
 * @brief  LED 任务
 * @param  argument 任务参数
 */
static void led_task(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    DEBUG_PRINTF("led_task start");

    for (;;)
    {
        led_cmd_t cmd;
        while (xQueueReceive(g_led_cmd_queue, &cmd, 0) == pdPASS)
        {
            apply_status_t status = apply_led_cmd(&cmd);

            if (cmd.report_status && status == APPLY_OK)
            {
                proto_tx_req_t tx = {0};
                tx.type = PROTO_TX_LED_STATUS;
                (void)app_ipc_send_proto_tx(&tx, pdMS_TO_TICKS(10U));
            }

            if (cmd.need_result)
            {
                cmd_result_t result =
                {
                    .seq = cmd.seq,
                    .cmd = cmd.proto_cmd,
                    .status = status,
                };
                (void)app_ipc_send_apply_result(&result, pdMS_TO_TICKS(10U));
            }
        }

        led_mgr_tick(50U);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(50U));
    }
}

/**
 * @brief  配置存储任务
 * @param  argument 任务参数
 */
static void storage_task(void *argument)
{
    (void)argument;
    DEBUG_PRINTF("storage_task start");

    for (;;)
    {
        storage_cmd_t cmd;
        if (xQueueReceive(g_storage_cmd_queue, &cmd, portMAX_DELAY) == pdPASS)
        {
            apply_status_t status = apply_storage_cmd(&cmd);

            if (cmd.need_result)
            {
                cmd_result_t result =
                {
                    .seq = cmd.seq,
                    .cmd = cmd.proto_cmd,
                    .status = status,
                };
                (void)app_ipc_send_apply_result(&result, pdMS_TO_TICKS(10U));
            }
        }
    }
}

/**
 * @brief  CLI 与异步日志输出任务
 * @param  argument 任务参数
 */
static void cli_task(void *argument)
{
    (void)argument;
    DEBUG_PRINTF("cli_task start");

    for (;;)
    {
        cli_poll();

        debug_log_item_t item;
        while (xQueueReceive(g_debug_log_queue, &item, 0) == pdPASS)
        {
            debug_console_write(item.text);
            debug_console_write("\r\n");
        }

        vTaskDelay(pdMS_TO_TICKS(2U));
    }
}

/* 看门狗任务：高于被监控任务的优先级，定期轮询 */
void watchdog_task(void *argument)
{
	(void)argument;
    DEBUG_PRINTF("watchdog_monitor_task start");

	const TickType_t xPeriod = pdMS_TO_TICKS(1000); /* 每1秒检查一次 */
	TickType_t xLastWakeTime = xTaskGetTickCount();

	for (;;) 
	{
				wdt_monitor_check();

				/* 用 vTaskDelayUntil 保证固定周期，避免累积误差 */
				xTaskDelayUntil(&xLastWakeTime, xPeriod);
	}
}

/**
 * @brief  创建静态任务
 * @param  function 任务函数
 * @param  name 任务名
 * @param  stack_words 栈大小
 * @param  priority 优先级
 * @param  tcb 静态 TCB
 * @param  stack 静态栈
 * @param  handle 返回句柄
 * @return pdPASS 成功
 */
static BaseType_t create_static_task(TaskFunction_t function,
                                     const char *name,
                                     UBaseType_t stack_words,
                                     UBaseType_t priority,
                                     StaticTask_t *tcb,
                                     StackType_t *stack,
                                     TaskHandle_t *handle)
{
    TaskHandle_t created = xTaskCreateStatic(function,
                                             name,
                                             stack_words,
                                             NULL,
                                             priority,
                                             stack,
                                             tcb);
    if (created == NULL)
    {
        return pdFAIL;
    }

    *handle = created;
    return pdPASS;
}

/**
 * @brief  创建全部任务
 */
static void tasks_init(void)
{
    configASSERT(create_static_task(comm_task,
                                    "comm",
                                    COMM_TASK_STACK_WORDS,
                                    COMM_TASK_PRIORITY,
                                    &comm_tcb,
                                    comm_stack,
                                    &comm_task_handle) == pdPASS);

    configASSERT(create_static_task(key_task,
                                    "key",
                                    KEY_TASK_STACK_WORDS,
                                    KEY_TASK_PRIORITY,
                                    &key_tcb,
                                    key_stack,
                                    &key_task_handle) == pdPASS);

    configASSERT(create_static_task(display_task,
                                    "display",
                                    DISPLAY_TASK_STACK_WORDS,
                                    DISPLAY_TASK_PRIORITY,
                                    &display_tcb,
                                    display_stack,
                                    &display_task_handle) == pdPASS);

    configASSERT(create_static_task(cli_task,
                                    "cli",
                                    CLI_TASK_STACK_WORDS,
                                    CLI_TASK_PRIORITY,
                                    &cli_tcb,
                                    cli_stack,
                                    &cli_task_handle) == pdPASS);

    configASSERT(create_static_task(led_task,
                                    "led",
                                    LED_TASK_STACK_WORDS,
                                    LED_TASK_PRIORITY,
                                    &led_tcb,
                                    led_stack,
                                    &led_task_handle) == pdPASS);

    configASSERT(create_static_task(storage_task,
                                    "storage",
                                    STORAGE_TASK_STACK_WORDS,
                                    STORAGE_TASK_PRIORITY,
                                    &storage_tcb,
                                    storage_stack,
                                    &storage_task_handle) == pdPASS);

    configASSERT(create_static_task(watchdog_task,
                                    "watchdog",
                                    WATCHDOG_TASK_STACK_WORDS,
                                    WATCHDOG_TASK_PRIORITY,
                                    &watchdog_tcb,
                                    watchdog_stack,
                                    &watchdog_task_handle) == pdPASS);
}

/**
 * @brief  cli_tasks_info — 显示所有任务状态
 * @param  argc 参数个数
 * @param  argv 参数数组
 * @return 0=成功
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
        const char *state_str = "INV";
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
 * @brief  初始化静态队列和任务
 */
void freertos_app_init(void)
{
    configASSERT(app_ipc_init() == pdPASS);
    tasks_init();
    app_ipc_set_comm_task(comm_task_handle);
    (void)cli_cmd_register("tasks_info", cli_tasks_info, "显示任务状态/优先级/栈剩余空间");
}

/**
 * @brief  开启任务调度
 */
void freertos_task_start(void)
{
    vTaskStartScheduler();
}

/**
 * @brief  栈溢出 Hook
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
 * @brief  堆分配失败 Hook
 */
void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}
