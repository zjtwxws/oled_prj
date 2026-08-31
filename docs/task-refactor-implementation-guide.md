# user_app_handle 多任务重构手工执行指南

> 状态：手工修改指南  
> 适用目录：`stm32f407/`  
> 目标：把 `user_app_handle()` 拆分为串口、按键、显示、LED、CLI、存储、看门狗任务，并使用静态任务、静态队列、ISR 通知、异步日志和 ACK 实际应用闭环。

## 1. 总体目标与任务表

| 任务 | 优先级 | 静态栈 | 周期/唤醒方式 |
|---|---:|---:|---|
| `comm_task` | `5` | 1024 words | UART1 ISR 通知，超时 2ms |
| `key_task` | `4` | 512 words | `vTaskDelayUntil(20ms)` |
| `display_task` | `3` | 1024 words | `vTaskDelayUntil(50ms)`，每次先清空显示命令队列 |
| `cli_task` | `2` | 512 words | `vTaskDelay(2ms)` |
| `led_task` | `1` | 256 words | `vTaskDelayUntil(50ms)`，每次先清空 LED 命令队列 |
| `storage_task` | `1` | 512 words | 阻塞等待存储命令队列 |
| `watchdog_task` | `0` | 256 words | `vTaskDelayUntil(200ms)` |

FreeRTOS 任务优先级 `0` 最低，`7` 最高。当前 `configMAX_PRIORITIES=8`，上述优先级均可用。

## 2. 新增文件

### 2.1 新增 `stm32f407/inc/app_ipc.h`

该文件集中定义任务间命令、结果和队列句柄。

```c
/**
 * @file    app_ipc.h
 * @brief   多任务间 IPC 类型、队列句柄与发送接口
 */

#ifndef __APP_IPC_H
#define __APP_IPC_H

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "display_mgr.h"
#include "led_mgr.h"
#include "protocol.h"

#define DISP_TEXT_MAX         256
#define DISP_BOOT_TEXT_MAX    128
#define DISP_FRAME_SEG_MAX    200
#define STORAGE_TEXT_MAX      128
#define DEBUG_LOG_TEXT_MAX    128

#define DISP_CMD_QUEUE_LEN    4
#define LED_CMD_QUEUE_LEN     8
#define STORAGE_CMD_QUEUE_LEN 4
#define PROTO_TX_QUEUE_LEN    8
#define CMD_RESULT_QUEUE_LEN  8
#define DEBUG_LOG_QUEUE_LEN   16

typedef enum
{
    DISP_CMD_SET_REMOTE = 0,
    DISP_CMD_SET_REMOTE_SUB,
    DISP_CMD_SET_MODE,
    DISP_CMD_NEXT_MODE,
    DISP_CMD_SET_SUB_MODE,
    DISP_CMD_SET_TEXT,
    DISP_CMD_SET_BOOT_TEXT,
    DISP_CMD_UPDATE_STATUS,
    DISP_CMD_FRAME_SEG,
    DISP_CMD_SHOW_DISCONNECT,
    DISP_CMD_HIDE_DISCONNECT,
    DISP_CMD_REDRAW,
    DISP_CMD_SET_CONTRAST,
} disp_cmd_type_t;

typedef struct
{
    disp_cmd_type_t type;
    uint8_t seq;
    uint8_t proto_cmd;
    bool need_result;
    bool report_status;

    union
    {
        bool remote;
        struct
        {
            bool remote;
            uint8_t sub_mode;
        } remote_sub;
        display_mode_t mode;
        uint8_t sub_mode;
        uint8_t contrast;
        char text[DISP_TEXT_MAX];
        char boot_text[DISP_BOOT_TEXT_MAX];
        display_status_t status;
        struct
        {
            uint8_t seg;
            uint8_t total;
            uint8_t len;
            uint8_t data[DISP_FRAME_SEG_MAX];
        } frame_seg;
    } u;
} disp_cmd_t;

typedef enum
{
    LED_CMD_SET_STATE = 0,
    LED_CMD_NEXT_MODE,
} led_cmd_type_t;

typedef struct
{
    led_cmd_type_t type;
    led_state_t state;
    uint8_t seq;
    uint8_t proto_cmd;
    bool need_result;
    bool report_status;
} led_cmd_t;

typedef enum
{
    STORAGE_CMD_BOOT_TEXT = 0,
    STORAGE_CMD_POWERON_TYPE,
} storage_cmd_type_t;

typedef struct
{
    storage_cmd_type_t type;
    uint8_t seq;
    uint8_t proto_cmd;
    bool need_result;
    union
    {
        char text[STORAGE_TEXT_MAX];
        uint8_t poweron_type;
    } u;
} storage_cmd_t;

typedef enum
{
    APPLY_OK = 0,
    APPLY_PARAM_ERROR,
    APPLY_BUSY,
    APPLY_FLASH_ERROR,
} apply_status_t;

typedef struct
{
    uint8_t seq;
    uint8_t cmd;
    apply_status_t status;
} cmd_result_t;

typedef enum
{
    PROTO_TX_KEY_EVENT = 0,
    PROTO_TX_LED_STATUS,
    PROTO_TX_MODE_STATUS,
} proto_tx_type_t;

typedef struct
{
    proto_tx_type_t type;
    uint8_t a;
    uint8_t b;
} proto_tx_req_t;

typedef struct
{
    char text[DEBUG_LOG_TEXT_MAX];
} debug_log_item_t;

extern QueueHandle_t g_disp_cmd_queue;
extern QueueHandle_t g_led_cmd_queue;
extern QueueHandle_t g_storage_cmd_queue;
extern QueueHandle_t g_proto_tx_queue;
extern QueueHandle_t g_cmd_result_queue;
extern QueueHandle_t g_debug_log_queue;

BaseType_t app_ipc_init(void);
void app_ipc_set_comm_task(TaskHandle_t task);
void app_ipc_notify_comm_from_isr(BaseType_t *woken);
void app_ipc_notify_comm(void);

BaseType_t app_ipc_send_disp_cmd(const disp_cmd_t *cmd, TickType_t wait);
BaseType_t app_ipc_send_led_cmd(const led_cmd_t *cmd, TickType_t wait);
BaseType_t app_ipc_send_storage_cmd(const storage_cmd_t *cmd, TickType_t wait);
BaseType_t app_ipc_send_proto_tx(const proto_tx_req_t *req, TickType_t wait);
BaseType_t app_ipc_send_apply_result(const cmd_result_t *result, TickType_t wait);
BaseType_t app_ipc_send_debug_log(const char *text);

#endif
```

### 2.2 新增 `stm32f407/src/app_ipc.c`

```c
/**
 * @file    app_ipc.c
 * @brief   多任务间静态队列创建与发送封装
 */

#include "app_ipc.h"

#include <string.h>

static TaskHandle_t comm_task = NULL;

static uint8_t disp_cmd_storage[DISP_CMD_QUEUE_LEN * sizeof(disp_cmd_t)];
static uint8_t led_cmd_storage[LED_CMD_QUEUE_LEN * sizeof(led_cmd_t)];
static uint8_t storage_cmd_storage[STORAGE_CMD_QUEUE_LEN * sizeof(storage_cmd_t)];
static uint8_t proto_tx_storage[PROTO_TX_QUEUE_LEN * sizeof(proto_tx_req_t)];
static uint8_t cmd_result_storage[CMD_RESULT_QUEUE_LEN * sizeof(cmd_result_t)];
static uint8_t debug_log_storage[DEBUG_LOG_QUEUE_LEN * sizeof(debug_log_item_t)];

static StaticQueue_t disp_cmd_ctrl;
static StaticQueue_t led_cmd_ctrl;
static StaticQueue_t storage_cmd_ctrl;
static StaticQueue_t proto_tx_ctrl;
static StaticQueue_t cmd_result_ctrl;
static StaticQueue_t debug_log_ctrl;

QueueHandle_t g_disp_cmd_queue = NULL;
QueueHandle_t g_led_cmd_queue = NULL;
QueueHandle_t g_storage_cmd_queue = NULL;
QueueHandle_t g_proto_tx_queue = NULL;
QueueHandle_t g_cmd_result_queue = NULL;
QueueHandle_t g_debug_log_queue = NULL;

/**
 * @brief  创建全部静态队列
 * @return pdPASS 成功，其他值失败
 */
BaseType_t app_ipc_init(void)
{
    g_disp_cmd_queue = xQueueCreateStatic(DISP_CMD_QUEUE_LEN,
                                          sizeof(disp_cmd_t),
                                          disp_cmd_storage,
                                          &disp_cmd_ctrl);
    configASSERT(g_disp_cmd_queue != NULL);

    g_led_cmd_queue = xQueueCreateStatic(LED_CMD_QUEUE_LEN,
                                         sizeof(led_cmd_t),
                                         led_cmd_storage,
                                         &led_cmd_ctrl);
    configASSERT(g_led_cmd_queue != NULL);

    g_storage_cmd_queue = xQueueCreateStatic(STORAGE_CMD_QUEUE_LEN,
                                             sizeof(storage_cmd_t),
                                             storage_cmd_storage,
                                             &storage_cmd_ctrl);
    configASSERT(g_storage_cmd_queue != NULL);

    g_proto_tx_queue = xQueueCreateStatic(PROTO_TX_QUEUE_LEN,
                                          sizeof(proto_tx_req_t),
                                          proto_tx_storage,
                                          &proto_tx_ctrl);
    configASSERT(g_proto_tx_queue != NULL);

    g_cmd_result_queue = xQueueCreateStatic(CMD_RESULT_QUEUE_LEN,
                                            sizeof(cmd_result_t),
                                            cmd_result_storage,
                                            &cmd_result_ctrl);
    configASSERT(g_cmd_result_queue != NULL);

    g_debug_log_queue = xQueueCreateStatic(DEBUG_LOG_QUEUE_LEN,
                                           sizeof(debug_log_item_t),
                                           debug_log_storage,
                                           &debug_log_ctrl);
    configASSERT(g_debug_log_queue != NULL);

    return pdPASS;
}

/**
 * @brief  设置 UART1 接收通知目标任务
 * @param  task 串口任务句柄
 */
void app_ipc_set_comm_task(TaskHandle_t task)
{
    comm_task = task;
}

/**
 * @brief  在 ISR 中唤醒串口任务
 * @param  woken 高优先级任务唤醒标志
 */
void app_ipc_notify_comm_from_isr(BaseType_t *woken)
{
    if (comm_task != NULL)
    {
        vTaskNotifyGiveFromISR(comm_task, woken);
    }
}

/**
 * @brief  在任务上下文中唤醒串口任务
 */
void app_ipc_notify_comm(void)
{
    if (comm_task != NULL)
    {
        xTaskNotifyGive(comm_task);
    }
}

/**
 * @brief  投递显示命令
 */
BaseType_t app_ipc_send_disp_cmd(const disp_cmd_t *cmd, TickType_t wait)
{
    if (cmd == NULL || g_disp_cmd_queue == NULL)
    {
        return pdFAIL;
    }
    return xQueueSend(g_disp_cmd_queue, cmd, wait);
}

/**
 * @brief  投递 LED 命令
 */
BaseType_t app_ipc_send_led_cmd(const led_cmd_t *cmd, TickType_t wait)
{
    if (cmd == NULL || g_led_cmd_queue == NULL)
    {
        return pdFAIL;
    }
    return xQueueSend(g_led_cmd_queue, cmd, wait);
}

/**
 * @brief  投递存储命令
 */
BaseType_t app_ipc_send_storage_cmd(const storage_cmd_t *cmd, TickType_t wait)
{
    if (cmd == NULL || g_storage_cmd_queue == NULL)
    {
        return pdFAIL;
    }
    return xQueueSend(g_storage_cmd_queue, cmd, wait);
}

/**
 * @brief  投递协议发送请求，并唤醒串口任务
 */
BaseType_t app_ipc_send_proto_tx(const proto_tx_req_t *req, TickType_t wait)
{
    if (req == NULL || g_proto_tx_queue == NULL)
    {
        return pdFAIL;
    }

    BaseType_t ret = xQueueSend(g_proto_tx_queue, req, wait);
    if (ret == pdPASS)
    {
        app_ipc_notify_comm();
    }
    return ret;
}

/**
 * @brief  投递命令应用结果
 */
BaseType_t app_ipc_send_apply_result(const cmd_result_t *result, TickType_t wait)
{
    if (result == NULL || g_cmd_result_queue == NULL)
    {
        return pdFAIL;
    }
    return xQueueSend(g_cmd_result_queue, result, wait);
}

/**
 * @brief  投递异步调试日志
 */
BaseType_t app_ipc_send_debug_log(const char *text)
{
    if (text == NULL || g_debug_log_queue == NULL)
    {
        return pdFAIL;
    }

    debug_log_item_t item = {0};
    strncpy(item.text, text, DEBUG_LOG_TEXT_MAX - 1);
    item.text[DEBUG_LOG_TEXT_MAX - 1] = '\0';
    return xQueueSend(g_debug_log_queue, &item, 0);
}
```

## 3. 修改 `stm32f407/src/freertos_app.c`

建议整文件替换为下面内容，保留 `tasks_info`、栈溢出 Hook 和堆失败 Hook。

```c
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
#include "iwdg_drv.h"
#include "ssd1306.h"

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
 * @brief  看门狗任务
 */
static void watchdog_task(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    DEBUG_PRINTF("watchdog_task start");

    for (;;)
    {
        iwdg_drv_feed();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(200U));
    }
}

/**
 * @brief  CLI 与异步日志输出任务
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

/**
 * @brief  创建静态任务
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
```

注意：原 `cli_task` 是外部符号，这里改为 `static`，不影响其他文件，因为 [freertos_app.h](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/inc/freertos_app.h) 没有声明它。

## 4. 修改 `stm32f407/src/user_app.c`

### 4.1 头文件与接口

在 `user_app.c` 顶部增加：

```c
#include "app_ipc.h"
```

将 `user_app.h` 中的 `user_app_handle()` 声明替换为：

```c
void user_app_comm_process(void);
void user_app_key_process(void);
void user_app_send_proto_tx(const proto_tx_req_t *req);
```

`user_app.h` 需要包含 `app_ipc.h`，或者前向声明相关结构后由 `user_app.c` 包含。建议直接：

```c
#include "app_ipc.h"
```

### 4.2 删除 `user_app_handle`

删除 [user_app.c:89](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/src/user_app.c:89) 至 [user_app.c:219](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/src/user_app.c:219) 的整个 `user_app_handle()` 函数，替换为下面三个函数。

#### `user_app_comm_process`

```c
/**
 * @brief  串口任务处理：协议超时、接收字节、断线检测
 */
void user_app_comm_process(void)
{
    static uint32_t last_rx_tick = 0;
    static bool disconnect_shown = false;

    uint32_t bytes_read_this_loop = 0;
    uint8_t rx_byte;

    if (proto_get_last_byte_tick() > 0U &&
        sys_tick_ms() - proto_get_last_byte_tick() > PROTO_RX_TIMEOUT_MS)
    {
        proto_reset_rx();
    }

    while (uart_drv_available() > 0U)
    {
        uart_drv_read_byte(&rx_byte);
        bytes_read_this_loop++;

        if (proto_feed_byte(rx_byte) != 0)
        {
            const proto_frame_t *f = proto_get_frame();
            DEBUG_PRINTF("RX: cmd=0x%02X seq=%d len=%d", f->cmd, f->seq, f->len);
            process_frame(f);
        }
    }

    if (bytes_read_this_loop > 0U)
    {
        last_rx_tick = sys_tick_ms();
        if (disconnect_shown)
        {
            disconnect_shown = false;
            disp_cmd_t cmd = {0};
            cmd.type = DISP_CMD_HIDE_DISCONNECT;
            (void)app_ipc_send_disp_cmd(&cmd, 0);
        }
    }

    if (!menu_mgr_is_active() && display_mgr_is_remote() &&
        last_rx_tick > 0U &&
        sys_tick_ms() - last_rx_tick > 5000U &&
        !disconnect_shown)
    {
        disconnect_shown = true;
        DEBUG_PRINTF("MODE: serial disconnected");
        disp_cmd_t cmd = {0};
        cmd.type = DISP_CMD_SHOW_DISCONNECT;
        (void)app_ipc_send_disp_cmd(&cmd, 0);
    }
}
```

说明：这里 `display_mgr_is_remote()` 和 `menu_mgr_is_active()` 仍然是读状态判断。由于 `comm_task` 优先级高于 `display_task`，单核上执行该判断时显示任务不会同时写这些状态。

#### `user_app_key_process`

```c
/**
 * @brief  按键任务处理：扫描与分发
 */
void user_app_key_process(void)
{
    static uint32_t last_key_scan = 0;
    key_info_t key_info;

    if (sys_tick_ms() - last_key_scan < 20U)
    {
        return;
    }
    last_key_scan = sys_tick_ms();

    if (key_drv_scan(&key_info) == 0)
    {
        return;
    }

    DEBUG_PRINTF("KEY: id=%d event=%d", key_info.key_id, key_info.event);

    if (menu_mgr_is_active())
    {
        menu_mgr_handle_key(key_info.key_id, key_info.event);
    }
    else
    {
        switch (key_info.key_id)
        {
        case 1:
        {
            disp_cmd_t cmd = {0};
            cmd.type = DISP_CMD_NEXT_MODE;
            cmd.report_status = true;
            (void)app_ipc_send_disp_cmd(&cmd, 0);
            break;
        }

        case 2:
        {
            led_cmd_t cmd = {0};
            cmd.type = LED_CMD_NEXT_MODE;
            cmd.report_status = true;
            (void)app_ipc_send_led_cmd(&cmd, 0);
            break;
        }

        case 4:
            if (key_info.event == KEY_EVENT_LONG_PRESS)
            {
                menu_mgr_activate();
            }
            break;

        default:
            break;
        }
    }

    if (key_info.event != KEY_EVENT_LONG_PRESS_REPEAT)
    {
        proto_tx_req_t tx = {0};
        tx.type = PROTO_TX_KEY_EVENT;
        tx.a = key_info.key_id;
        tx.b = (uint8_t)key_info.event;
        (void)app_ipc_send_proto_tx(&tx, 0);
    }
}
```

#### `user_app_send_proto_tx`

```c
/**
 * @brief  串口任务统一发送协议上行帧
 * @param  req 发送请求
 */
void user_app_send_proto_tx(const proto_tx_req_t *req)
{
    if (req == NULL)
    {
        return;
    }

    switch (req->type)
    {
    case PROTO_TX_KEY_EVENT:
        send_key_event(req->a, req->b);
        break;

    case PROTO_TX_LED_STATUS:
        send_led_status();
        break;

    case PROTO_TX_MODE_STATUS:
        send_mode_status();
        break;

    default:
        break;
    }
}
```

### 4.3 增加 ACK 等待辅助函数

在 `safe_send()` 附近增加：

```c
/**
 * @brief  等待命令拥有者任务返回应用结果
 * @param  seq       协议序列号
 * @param  cmd       协议命令字
 * @param  expected  需要收到的成功结果数量
 * @param  timeout_ms 最大等待时间
 * @return 应用结果
 */
static apply_status_t comm_wait_results(uint8_t seq,
                                        uint8_t cmd,
                                        uint8_t expected,
                                        uint32_t timeout_ms)
{
    uint32_t deadline = sys_tick_ms() + timeout_ms;
    uint8_t ok_count = 0;

    while (sys_tick_ms() < deadline)
    {
        cmd_result_t result;
        if (xQueueReceive(g_cmd_result_queue,
                          &result,
                          pdMS_TO_TICKS(10U)) != pdPASS)
        {
            continue;
        }

        if (result.seq != seq || result.cmd != cmd)
        {
            continue;
        }

        if (result.status != APPLY_OK)
        {
            return result.status;
        }

        ok_count++;
        if (ok_count >= expected)
        {
            return APPLY_OK;
        }
    }

    return APPLY_BUSY;
}
```

### 4.4 替换 `process_frame`

用下面版本替换现有 `process_frame()`。原 [process_frame()](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/src/user_app.c:226) 中的 `led_mgr_set_state()`、`display_mgr_set_*()`、`sys_config_set_boot_text()` 全部改为投递 IPC 命令。

```c
static void process_frame(const proto_frame_t *frame)
{
    switch (frame->cmd)
    {
    case CMD_LED_CTRL:
    {
        if (frame->len < 1U || frame->data[0] > 2U)
        {
            send_nak(frame->seq, NAK_PARAM_ERROR);
            break;
        }

        led_cmd_t cmd = {0};
        cmd.type = LED_CMD_SET_STATE;
        cmd.state = (led_state_t)frame->data[0];
        cmd.seq = frame->seq;
        cmd.proto_cmd = frame->cmd;
        cmd.need_result = true;

        if (app_ipc_send_led_cmd(&cmd, pdMS_TO_TICKS(20U)) != pdPASS)
        {
            send_nak(frame->seq, NAK_BUSY);
            break;
        }

        apply_status_t status = comm_wait_results(frame->seq, frame->cmd, 1U, 100U);
        if (status != APPLY_OK)
        {
            send_nak(frame->seq, NAK_BUSY);
            break;
        }

        send_ack(frame->seq);
        send_led_status();
        break;
    }

    case CMD_DISPLAY_MODE:
    {
        if (frame->len < 1U)
        {
            send_nak(frame->seq, NAK_PARAM_ERROR);
            break;
        }

        bool to_remote = (frame->data[0] & 0x80U) != 0U;
        uint8_t sub = frame->data[0] & 0x7FU;

        disp_cmd_t cmd = {0};
        cmd.seq = frame->seq;
        cmd.proto_cmd = frame->cmd;
        cmd.need_result = true;

        if (to_remote)
        {
            if (sub > REMOTE_SUB_DATE)
            {
                send_nak(frame->seq, NAK_PARAM_ERROR);
                break;
            }
            cmd.type = DISP_CMD_SET_REMOTE_SUB;
            cmd.u.remote_sub.remote = true;
            cmd.u.remote_sub.sub_mode = sub;
        }
        else
        {
            if (sub >= DISP_MODE_COUNT)
            {
                send_nak(frame->seq, NAK_PARAM_ERROR);
                break;
            }
            cmd.type = DISP_CMD_SET_MODE;
            cmd.u.mode = (display_mode_t)sub;
        }

        if (app_ipc_send_disp_cmd(&cmd, pdMS_TO_TICKS(20U)) != pdPASS)
        {
            send_nak(frame->seq, NAK_BUSY);
            break;
        }

        apply_status_t status = comm_wait_results(frame->seq, frame->cmd, 1U, 150U);
        if (status != APPLY_OK)
        {
            send_nak(frame->seq, NAK_BUSY);
            break;
        }

        send_ack(frame->seq);
        send_mode_status();
        break;
    }

    case CMD_TEXT_CONTENT:
    {
        if (frame->len == 0U || frame->len > PROTO_MAX_DATA)
        {
            send_nak(frame->seq, NAK_PARAM_ERROR);
            break;
        }

        disp_cmd_t cmd = {0};
        cmd.type = DISP_CMD_SET_TEXT;
        cmd.seq = frame->seq;
        cmd.proto_cmd = frame->cmd;
        cmd.need_result = true;
        memcpy(cmd.u.text, frame->data, frame->len);
        cmd.u.text[frame->len] = '\0';

        if (app_ipc_send_disp_cmd(&cmd, pdMS_TO_TICKS(20U)) != pdPASS)
        {
            send_nak(frame->seq, NAK_BUSY);
            break;
        }

        apply_status_t status = comm_wait_results(frame->seq, frame->cmd, 1U, 150U);
        if (status != APPLY_OK)
        {
            send_nak(frame->seq, NAK_BUSY);
            break;
        }

        send_ack(frame->seq);
        break;
    }

    case CMD_TIME_SYNC:
    {
        if (frame->len < 7U)
        {
            send_nak(frame->seq, NAK_PARAM_ERROR);
            break;
        }

        display_status_t st = {0};
        st.year = frame->data[0];
        st.month = frame->data[1];
        st.day = frame->data[2];
        st.hour = frame->data[3];
        st.minute = frame->data[4];
        st.second = frame->data[5];
        st.week_day = frame->data[6];

        disp_cmd_t cmd = {0};
        cmd.type = DISP_CMD_UPDATE_STATUS;
        cmd.seq = frame->seq;
        cmd.proto_cmd = frame->cmd;
        cmd.need_result = true;
        cmd.u.status = st;

        if (app_ipc_send_disp_cmd(&cmd, pdMS_TO_TICKS(20U)) != pdPASS)
        {
            send_nak(frame->seq, NAK_BUSY);
            break;
        }

        apply_status_t status = comm_wait_results(frame->seq, frame->cmd, 1U, 150U);
        if (status != APPLY_OK)
        {
            send_nak(frame->seq, NAK_BUSY);
            break;
        }

        send_ack(frame->seq);
        break;
    }

    case CMD_WEATHER_DATA:
    {
        if (frame->len < 4U)
        {
            send_nak(frame->seq, NAK_PARAM_ERROR);
            break;
        }

        display_status_t st = {0};
        st.weather_type = frame->data[0];
        st.temperature = (int8_t)frame->data[1];
        st.humidity = frame->data[2];
        st.wind_dir = frame->data[3];

        disp_cmd_t cmd = {0};
        cmd.type = DISP_CMD_UPDATE_STATUS;
        cmd.seq = frame->seq;
        cmd.proto_cmd = frame->cmd;
        cmd.need_result = true;
        cmd.u.status = st;

        if (app_ipc_send_disp_cmd(&cmd, pdMS_TO_TICKS(20U)) != pdPASS)
        {
            send_nak(frame->seq, NAK_BUSY);
            break;
        }

        apply_status_t status = comm_wait_results(frame->seq, frame->cmd, 1U, 150U);
        if (status != APPLY_OK)
        {
            send_nak(frame->seq, NAK_BUSY);
            break;
        }

        send_ack(frame->seq);
        break;
    }

    case CMD_FRAME_SYNC:
    {
        if (!display_mgr_is_remote())
        {
            send_nak(frame->seq, NAK_BUSY);
            break;
        }

        if (frame->len < 2U)
        {
            send_nak(frame->seq, NAK_PARAM_ERROR);
            break;
        }

        uint8_t seg = frame->data[0];
        uint8_t total = frame->data[1];
        uint8_t payload_len = (uint8_t)(frame->len - 2U);
        if (payload_len > DISP_FRAME_SEG_MAX)
        {
            send_nak(frame->seq, NAK_PARAM_ERROR);
            break;
        }

        disp_cmd_t cmd = {0};
        cmd.type = DISP_CMD_FRAME_SEG;
        cmd.seq = frame->seq;
        cmd.proto_cmd = frame->cmd;
        cmd.need_result = true;
        cmd.u.frame_seg.seg = seg;
        cmd.u.frame_seg.total = total;
        cmd.u.frame_seg.len = payload_len;
        memcpy(cmd.u.frame_seg.data, frame->data + 2U, payload_len);

        if (app_ipc_send_disp_cmd(&cmd, pdMS_TO_TICKS(20U)) != pdPASS)
        {
            send_nak(frame->seq, NAK_BUSY);
            break;
        }

        apply_status_t status = comm_wait_results(frame->seq, frame->cmd, 1U, 150U);
        if (status != APPLY_OK)
        {
            send_nak(frame->seq, status == APPLY_PARAM_ERROR ? NAK_PARAM_ERROR : NAK_BUSY);
            break;
        }

        send_ack(frame->seq);
        break;
    }

    case CMD_BOOT_TEXT:
    {
        if (frame->len == 0U || frame->len > STORAGE_TEXT_MAX)
        {
            send_nak(frame->seq, NAK_PARAM_ERROR);
            break;
        }

        storage_cmd_t store = {0};
        store.type = STORAGE_CMD_BOOT_TEXT;
        store.seq = frame->seq;
        store.proto_cmd = frame->cmd;
        store.need_result = true;
        memcpy(store.u.text, frame->data, frame->len);
        store.u.text[frame->len] = '\0';

        disp_cmd_t disp = {0};
        disp.type = DISP_CMD_SET_BOOT_TEXT;
        disp.seq = frame->seq;
        disp.proto_cmd = frame->cmd;
        disp.need_result = true;
        memcpy(disp.u.boot_text, frame->data, frame->len);
        disp.u.boot_text[frame->len] = '\0';

        if (app_ipc_send_storage_cmd(&store, pdMS_TO_TICKS(20U)) != pdPASS ||
            app_ipc_send_disp_cmd(&disp, pdMS_TO_TICKS(20U)) != pdPASS)
        {
            send_nak(frame->seq, NAK_BUSY);
            break;
        }

        apply_status_t status = comm_wait_results(frame->seq, frame->cmd, 2U, 800U);
        if (status == APPLY_FLASH_ERROR)
        {
            send_nak(frame->seq, NAK_FLASH_ERROR);
        }
        else if (status != APPLY_OK)
        {
            send_nak(frame->seq, NAK_BUSY);
        }
        else
        {
            send_ack(frame->seq);
        }
        break;
    }

    case CMD_OTA_RESERVED:
    {
        DEBUG_PRINTF("OTA: request received, rebooting to bootloader...");
        app_fw_info_set_ota_request();
        send_ack(frame->seq);
        sys_tick_delay_ms(100);
        sys_config_reset();
        break;
    }

    case CMD_ACK:
        break;

    case CMD_NAK:
        if (frame->len >= 1U)
        {
            DEBUG_PRINTF("NAK from host: code=0x%02X", frame->data[0]);
        }
        break;

    default:
        DEBUG_PRINTF("RX: unknown cmd=0x%02X", frame->cmd);
        send_nak(frame->seq, NAK_UNKNOWN_CMD);
        break;
    }
}
```

说明：

- `CMD_FRAME_SYNC` 的 ACK 表示“该分段已经被显示任务成功写入帧缓冲”；完整帧刷新由显示任务在最后一段完成后执行。
- `CMD_BOOT_TEXT` 等待 2 个成功结果：存储任务完成 Flash 写入，显示任务完成 boot text 状态更新。
- `CMD_OTA_RESERVED` 保持原语义：写 OTA 标志后立即 ACK，再延时复位。

## 5. 修改显示管理，分离状态更新和 OLED 刷屏

### 5.1 `stm32f407/inc/display_mgr.h`

增加：

```c
bool display_mgr_is_dirty(void);
void display_mgr_clear_dirty(void);
void display_mgr_flush(void);
```

将远程帧接口改为返回是否成功：

```c
bool display_mgr_rx_frame_seg(uint8_t seg, uint8_t total, const uint8_t *data, uint8_t len);
```

### 5.2 `stm32f407/src/display_mgr.c`

增加静态脏标记：

```c
static bool display_dirty = false;
```

增加函数：

```c
/**
 * @brief  查询是否有待刷新内容
 * @return true 有待刷新
 */
bool display_mgr_is_dirty(void)
{
    return display_dirty;
}

/**
 * @brief  清除脏标记
 */
void display_mgr_clear_dirty(void)
{
    display_dirty = false;
}

/**
 * @brief  由显示任务统一刷新 OLED
 */
void display_mgr_flush(void)
{
    if (display_dirty)
    {
        ssd1306_update_screen();
        display_dirty = false;
    }
}
```

逐项修改以下位置：

| 函数 | 当前行为 | 修改后 |
|---|---|---|
| `display_mgr_init()` | 调用 `ssd1306_update_screen()` | 保留，因为它在调度器启动前运行；完成后 `display_dirty = false` |
| `display_mgr_set_remote()` | 调用 `update_screen_if_allowed()` | 删除刷屏调用，绘制完设置 `display_dirty = true` |
| `display_mgr_rx_frame_seg()` | 完整帧时可能直接 `ssd1306_update_screen()` | 删除直接刷屏，完整帧拼好后设置 `display_dirty = true`；返回 `bool` |
| `display_mgr_set_text()` | 非远程时调用 `update_screen_if_allowed()` | 删除刷屏调用，设置 `display_dirty = true` |
| `display_mgr_set_mode()` | 非远程时调用 `update_screen_if_allowed()` | 删除刷屏调用，设置 `display_dirty = true` |
| `display_mgr_show_disconnect()` | 最后调用 `ssd1306_update_screen()` | 删除刷屏调用，设置 `display_dirty = true` |
| `display_mgr_tick()` | 特效分支直接调用 `ssd1306_update_screen()` | 删除直接刷屏，滚动/翻页/淡入淡出每次需要刷新时设置 `display_dirty = true` |
| `display_mgr_redraw()` | 远程和本地分支均直接 `ssd1306_update_screen()` | 删除直接刷屏，绘制完设置 `display_dirty = true` |

以 `display_mgr_set_remote()` 为例，修改后应为：

```c
void display_mgr_set_remote(bool remote)
{
    if (remote == is_remote)
    {
        return;
    }

    is_remote = remote;

    if (remote)
    {
        clear_full_screen();
        memset(frame_buf, 0, FRAME_BUF_SIZE);
        frame_seg_received = 0;
        frame_seg_total = 0;
        frame_seg_mask = 0;
        frame_rx_active = 0;
    }
    else
    {
        scroll_offset_x = 0;
        flip_phase = 0;
        fade_step = 0;
        fade_dir = 0;
        draw_text_fullscreen(content_text);
    }

    display_dirty = true;
}
```

`display_mgr_rx_frame_seg()` 需要把每个非法分支改为 `return false`，正常接收完成时：

```c
if (frame_seg_received >= frame_seg_total)
{
    uint8_t *buf = ssd1306_get_buffer();
    memcpy(buf, frame_buf, FRAME_BUF_SIZE);
    display_dirty = true;
    frame_seg_received = 0;
    frame_seg_mask = 0;
    frame_rx_active = 0;
}

return true;
```

## 6. 修改菜单，去掉按键上下文中的刷屏

### 6.1 `stm32f407/src/menu_mgr.c`

顶部增加：

```c
#include "app_ipc.h"
```

修改 `handle_confirm()` 中 `MENU_TYPE_PREVIEW`：

```c
case MENU_TYPE_PREVIEW:
    g_menu.preview_showing = true;
    g_menu.dirty = true;
    break;
```

删除其中的：

```c
if (item->preview.render)
{
    item->preview.render();
    ssd1306_update_screen();
}
```

修改 `menu_mgr_tick()` 中 `preview_showing` 分支：

```c
if (g_menu.preview_showing)
{
    const menu_item_t *item = g_menu.current_menu[g_menu.cursor];
    if (item->type == MENU_TYPE_PREVIEW && item->preview.render != NULL)
    {
        item->preview.render();
    }
    ssd1306_update_screen();
}
else if (g_menu.confirm_showing)
{
    render_confirm();
    ssd1306_update_screen();
}
else if (g_menu.info_showing)
{
    render_info();
    ssd1306_update_screen();
}
else
{
    render_menu();
    ssd1306_update_screen();
}
```

修改 `menu_mgr_activate()`：删除末尾的 `display_mgr_set_menu_suppress(true);`。显示任务会统一设置 `menu_suppress`。

修改 `menu_mgr_deactivate()`：删除 `display_mgr_redraw();`，改为投递显示命令：

```c
void menu_mgr_deactivate(void)
{
    g_menu.active = false;
    g_menu.dirty = false;
    g_menu.info_showing = false;
    g_menu.preview_showing = false;
    g_menu.confirm_showing = false;
    g_menu.value_editing = false;

    disp_cmd_t cmd = {0};
    cmd.type = DISP_CMD_REDRAW;
    (void)app_ipc_send_disp_cmd(&cmd, pdMS_TO_TICKS(20U));
}
```

`menu_mgr_init()` 中原来的 `display_mgr_set_menu_suppress(true);` 也可删除，因为显示任务运行时会在菜单激活分支重新设置。

### 6.2 `stm32f407/src/menu_items.c`

顶部增加：

```c
#include "app_ipc.h"
```

修改回调函数：

| 回调 | 修改后 |
|---|---|
| `cb_mode_changed()` | 投递 `DISP_CMD_SET_REMOTE` |
| `cb_disp_time()` | 投递 `DISP_CMD_SET_REMOTE_SUB`，`remote=true, sub=REMOTE_SUB_TIME` |
| `cb_disp_weather()` | 投递 `DISP_CMD_SET_REMOTE_SUB`，`sub=REMOTE_SUB_WEATHER` |
| `cb_disp_date()` | 投递 `DISP_CMD_SET_REMOTE_SUB`，`sub=REMOTE_SUB_DATE` |
| `cb_disp_custom()` | 投递 `DISP_CMD_SET_REMOTE_SUB`，`remote=true, sub=REMOTE_SUB_TEXT` |
| `cb_effect_*()` | 投递对应 `DISP_CMD_SET_MODE` |
| `cb_contrast_changed()` | 投递 `DISP_CMD_SET_CONTRAST` |
| `cb_led_off/on/blink()` | 投递 `LED_CMD_SET_STATE` |
| `preview_confirm_*()` | 投递 `STORAGE_CMD_POWERON_TYPE` |

示例：

```c
static void cb_mode_changed(uint8_t new_val)
{
    disp_cmd_t cmd = {0};
    cmd.type = DISP_CMD_SET_REMOTE;
    cmd.u.remote = (new_val != 0U);
    (void)app_ipc_send_disp_cmd(&cmd, 0);
}
```

```c
static void cb_led_on(void)
{
    led_cmd_t cmd = {0};
    cmd.type = LED_CMD_SET_STATE;
    cmd.state = LED_STATE_ON;
    (void)app_ipc_send_led_cmd(&cmd, 0);
}
```

```c
static void preview_confirm_welcome(void)
{
    g_poweron_type = SYS_CONFIG_POWERON_WELCOME;

    storage_cmd_t cmd = {0};
    cmd.type = STORAGE_CMD_POWERON_TYPE;
    cmd.u.poweron_type = SYS_CONFIG_POWERON_WELCOME;
    (void)app_ipc_send_storage_cmd(&cmd, 0);
}
```

删除 `preview_render_welcome()`、`preview_render_logo()`、`preview_render_bigtext()` 中的 `ssd1306_update_screen()`。菜单预览的实际刷屏由 `menu_mgr_tick()` 完成。

`cb_update_firmware()` 和 `cb_reboot_confirm()` 保持直接调用 `sys_config_reset()`，不需要投递队列。

## 7. 修改 `stm32f407/src/led_mgr.c/h`

LED 模块不需要新增队列类型，但要把状态操作限制在 `led_task` 中。当前 `led_mgr_set_state()` 和 `led_mgr_tick()` 可以直接保留，只要其他任务不再直接调用即可。

需要修改的旧调用点：

- [user_app.c:166](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/src/user_app.c:166) 的按键 LED 切换改为投递 `led_cmd_t`。
- [user_app.c:235](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/src/user_app.c:235) 的协议 LED 控制改为投递 `led_cmd_t`。
- [cli_cmds.c:107](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/src/cli_cmds.c:107) 的 CLI `led` 命令改为投递 `led_cmd_t`。
- [menu_items.c](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/src/menu_items.c) 的 LED 菜单回调改为投递 `led_cmd_t`。

## 8. 修改 `stm32f407/src/sys_config.c/h`，让 Flash 结果可返回

### 8.1 `stm32f407/inc/sys_config.h`

将两个函数改为返回 `int`：

```c
int sys_config_set_boot_text(const char *text);
int sys_config_set_poweron_type(uint8_t type);
```

### 8.2 `stm32f407/src/sys_config.c`

`sys_config_set_boot_text()` 修改为：

```c
int sys_config_set_boot_text(const char *text)
{
    if (text == NULL)
    {
        return -1;
    }

    if (strncmp(config.boot_text, text, SYS_CONFIG_BOOT_TEXT_LEN) == 0)
    {
        return 0;
    }

    strncpy(config.boot_text, text, SYS_CONFIG_BOOT_TEXT_LEN - 1);
    config.boot_text[SYS_CONFIG_BOOT_TEXT_LEN - 1] = '\0';
    config.crc32 = crc32_simple((const uint8_t *)&config, sizeof(sys_config_t) - 4U);

    return sys_config_save();
}
```

`sys_config_set_poweron_type()` 修改为：

```c
int sys_config_set_poweron_type(uint8_t type)
{
    if (type > 2U)
    {
        return -1;
    }

    if (config.poweron_type == type)
    {
        return 0;
    }

    config.poweron_type = type;
    config.crc32 = crc32_simple((const uint8_t *)&config, sizeof(sys_config_t) - 4U);

    return sys_config_save();
}
```

## 9. 修改 UART 驱动，加入 ISR 通知

### 9.1 `stm32f407/src/uart_drv.c`

顶部增加：

```c
#include "app_ipc.h"
```

在 `uart_drv_rx_callback()` 末尾，重新启动 `HAL_UART_Receive_IT()` 之后增加：

```c
BaseType_t xHigherPriorityTaskWoken = pdFALSE;
app_ipc_notify_comm_from_isr(&xHigherPriorityTaskWoken);
portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
```

这样 `HAL_UART_RxCpltCallback()` 每收到一个 UART1 字节，都会在写入环形缓冲后唤醒 `comm_task`。`app_ipc_notify_comm_from_isr()` 内部会判断 `comm_task` 句柄是否已设置，因此在 `user_app_init()` 早期、任务尚未创建时不会访问空句柄。

## 10. 修改调试串口，使用异步日志

### 10.1 `stm32f407/src/debug_console.c`

顶部增加：

```c
#include "app_ipc.h"
```

修改 `debug_printf()`：

```c
void debug_printf(const char *fmt, ...)
{
    char buf[DEBUG_LOG_TEXT_MAX];
    va_list args;

    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len < 0)
    {
        return;
    }

    if (g_debug_log_queue != NULL)
    {
        (void)app_ipc_send_debug_log(buf);
        return;
    }

    if (p_debug_uart != NULL)
    {
        HAL_UART_Transmit(p_debug_uart, (uint8_t *)buf, (uint16_t)strlen(buf), HAL_MAX_DELAY);
        HAL_UART_Transmit(p_debug_uart, (uint8_t *)"\r\n", 2U, HAL_MAX_DELAY);
    }
}
```

说明：

- 调度器启动后，`DEBUG_PRINTF` 只投递日志队列，不直接操作 UART2 TX。
- `user_app_init()` 早期队列尚未创建时，仍回退到直接 UART 输出。
- `shell_putc()`、`debug_console_write()` 保留直接输出，但只能由 `cli_task` 调用。

## 11. 修改 CLI 命令，避免跨任务直接操作硬件

### 11.1 `stm32f407/src/cli_cmds.c`

顶部增加：

```c
#include "app_ipc.h"
```

`cmd_led()` 改为：

```c
static int cmd_led(uint8_t argc, char **argv)
{
    if (argc < 2)
    {
        shell_printf("Usage: led <0|1|2>\r\n");
        return -1;
    }

    int state = atoi(argv[1]);
    if (state < 0 || state > 2)
    {
        shell_printf("Error: invalid state %d (must be 0-2)\r\n", state);
        return -1;
    }

    led_cmd_t cmd = {0};
    cmd.type = LED_CMD_SET_STATE;
    cmd.state = (led_state_t)state;
    (void)app_ipc_send_led_cmd(&cmd, 0);

    shell_printf("LED set to %d\r\n", state);
    return 0;
}
```

`cmd_mode()` 中两个模式切换改为：

```c
disp_cmd_t cmd = {0};
cmd.type = DISP_CMD_SET_REMOTE;

if (strcmp(argv[1], "local") == 0)
{
    cmd.u.remote = false;
    (void)app_ipc_send_disp_cmd(&cmd, 0);
    shell_printf("Switched to local mode\r\n");
}
else if (strcmp(argv[1], "remote") == 0)
{
    cmd.u.remote = true;
    (void)app_ipc_send_disp_cmd(&cmd, 0);
    shell_printf("Switched to remote mode\r\n");
}
```

`cmd_update()`、`cmd_reboot()` 继续直接调用 `sys_config_reset()`。

## 12. 启用看门狗

打开 [iwdg_drv.h](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/inc/iwdg_drv.h)：

```c
#define IWDG_ENABLE  1
```

`watchdog_task` 已按优先级 `0` 每 200ms 喂狗。任何优先级高于 `0` 的任务进入持续忙循环时，都会使该任务得不到执行，从而触发复位。

## 13. 构建配置更新

### 13.1 Keil 工程

在 `oled_cubemx`、`oled_cubemx_slota`、`oled_cubemx_slotb` 三个 target 的 `Application/User` 分组中加入：

```text
stm32f407/src/app_ipc.c
```

并确保头文件路径包含：

```text
stm32f407/inc
```

### 13.2 GNU Makefile

如果使用 [Makefile](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/Makefile)，需要检查该 Makefile 是否已包含 FreeRTOS 源码。当前 `APP_SRC` 还没有包含 `freertos_app.c` 和 FreeRTOS 内核源文件，因此本次迁移若使用 Makefile，应同步补齐。至少新增：

```make
src/app_ipc.c
src/freertos_app.c
```

并按实际 FreeRTOS 源路径补充 `thirdparty/freertos/*.c` 和 `portable/GCC/ARM_CM4F/port.c`、`portable/MemMang/heap_4.c`。

## 14. 手工修改顺序建议

1. 启用 `IWDG_ENABLE`。
2. 新增 `app_ipc.h`、`app_ipc.c`。
3. 修改 `sys_config.h/c` 的返回值。
4. 修改 `display_mgr.h/c` 的 dirty/flush 和 `rx_frame_seg` 返回值。
5. 修改 `menu_mgr.c` 的预览与菜单激活/退出刷屏逻辑。
6. 修改 `menu_items.c` 的回调为 IPC 投递。
7. 修改 `user_app.h/c`，删除 `user_app_handle()`，增加串口/按键/发送接口并重构 `process_frame()`。
8. 修改 `uart_drv.c` 加入 ISR 通知。
9. 修改 `debug_console.c` 加入异步日志。
10. 修改 `cli_cmds.c` 的 LED/显示命令。
11. 替换 `freertos_app.c` 为静态任务版本。
12. 在 Keil 或 Makefile 中加入新文件。
13. 编译三个 Keil target，依次验证启动、菜单、按键、LED、UART1、CLI、看门狗、OTA。

## 15. 验证清单

- `tasks_info` 能看到 7 个业务任务和 IDLE。
- 各任务栈高水位不为 0，且余量充足。
- UART1 长时间连续收发不丢帧，ACK/NAK 顺序正确。
- `CMD_BOOT_TEXT` 只有在 Flash 写入成功后才返回 ACK。
- 菜单预览进入/退出时，显示任务负责实际刷屏。
- 调试日志在多任务运行时不出现字符交错。
- IWDG 正常喂狗；人为让高优先级任务空转时设备会复位。
- OTA 请求和软件重启功能仍正常。

