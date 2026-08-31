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
