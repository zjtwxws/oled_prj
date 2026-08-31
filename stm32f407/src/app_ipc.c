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
 * @param  cmd 显示命令
 * @param  wait 等待时间
 * @return pdPASS 成功
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
 * @param  cmd LED 命令
 * @param  wait 等待时间
 * @return pdPASS 成功
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
 * @param  cmd 存储命令
 * @param  wait 等待时间
 * @return pdPASS 成功
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
 * @param  req 发送请求
 * @param  wait 等待时间
 * @return pdPASS 成功
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
 * @param  result 命令应用结果
 * @param  wait 等待时间
 * @return pdPASS 成功
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
 * @param  text 日志文本
 * @return pdPASS 成功
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
