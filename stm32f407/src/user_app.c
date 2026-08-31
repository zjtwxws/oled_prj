/**
 * @file    user_app.c
 * @brief   用户主程序 — OLED 三级联动项目
 */

#include <string.h>

#include "i2c_drv.h"
#include "ssd1306.h"
#include "uart_drv.h"
#include "protocol.h"
#include "display_mgr.h"
#include "led_mgr.h"
#include "key_drv.h"
#include "user_app.h"
#include "sys_config.h"
#include "sys_tick.h"
#include "debug_console.h"
#include "menu_mgr.h"
#include "app_fw_info.h"
#include "app_ipc.h"
#include "iwdg.h"

extern I2C_HandleTypeDef  hi2c2;  /* I2C2: SSD1306 OLED 通信 */
extern UART_HandleTypeDef huart1; /* USART1: PC 上位机通信 */
extern UART_HandleTypeDef huart2; /* USART2: 调试打印信息 */

static void process_frame(const proto_frame_t *frame);
static void send_ack(uint8_t seq);
static void send_nak(uint8_t seq, uint8_t error_code);
static void send_key_event(uint8_t key_id, uint8_t action);
static void send_led_status(void);
static void send_mode_status(void);
static apply_status_t comm_wait_results(uint8_t seq,
                                        uint8_t cmd,
                                        uint8_t expected,
                                        uint32_t timeout_ms);

static volatile uint8_t tx_seq = 0;  /* 发送帧序列号 (每次发送递增，用于协议层去重和确认) */

/*
 * 发送辅助函数: 所有 send_* 调用均在主循环上下文中,
 * UART RX ISR 只写环形缓冲区, 不碰 tx_buf, 无需关中断。
 */
/* 安全发送封装: 所有 send_* 调用在主循环上下文中，ISR 只写环形缓冲区不碰 tx_buf，无需关中断 */
/**
 * @brief  安全发送封装（ISR 安全）
 * @param  data 参数说明
 * @param  len 参数说明
 * @date   2026-08-07
 */
static void safe_send(const uint8_t *data, uint16_t len)
{
    uart_drv_send(data, len);
}

/**
 * @brief  用户应用初始化
 * @date   2026-08-07
 */
int user_app_init(void)
{
    i2c_drv_init(&hi2c2);   /* I2C2: SSD1306 OLED 通信 */
    uart_drv_init(&huart1);  /* USART1: 与 PC 上位机通信 (115200, 中断接收) */
    debug_console_init(&huart2);
    cli_init();

    ssd1306_init();
    sys_config_init();

    led_mgr_init();
    key_drv_init();
    
    display_mgr_init(sys_config_get_boot_text());
	
    /* 上电画面保持 2s，让用户看清启动内容 */
	for (uint32_t i = 0; i < 10; i++)
	{
		iwdg_drv_feed();
		sys_tick_delay_ms(200);
	}
	
    menu_mgr_init();
    
    DEBUG_PRINTF("Firmware Version: " FW_VERSION);
    DEBUG_PRINTF("SYSTEM: Boot complete, entering main loop");

    return 0;
}

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

/**
 * @brief  处理接收到的协议帧
 * @param  frame 参数说明
 * @date   2026-08-07
 */
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
        if (frame->len == 0U || frame->len >= STORAGE_TEXT_MAX)
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

/*
 * send_ack / send_nak / send_key_event / send_led_status / send_mode_status
 * 使用 safe_send() 替代裸 uart_drv_send, 防止 proto_build_frame → uart_drv_send
 * 之间被 ISR 覆盖全局 tx_buf。
 */
/**
 * @brief  发送 ACK 应答帧
 * @param  seq 参数说明
 * @date   2026-08-07
 */
static void send_ack(uint8_t seq)
{
    uint16_t len = proto_build_frame(CMD_ACK, seq, NULL, 0);
    safe_send(proto_get_tx_buf(), len);
}

/**
 * @brief  发送 NAK 应答帧
 * @param  seq 参数说明
 * @param  error_code 参数说明
 * @date   2026-08-07
 */
static void send_nak(uint8_t seq, uint8_t error_code)
{
    uint16_t len = proto_build_frame(CMD_NAK, seq, &error_code, 1);
    safe_send(proto_get_tx_buf(), len);
}

/**
 * @brief  发送按键事件帧
 * @param  key_id 参数说明
 * @param  action 参数说明
 * @date   2026-08-07
 */
static void send_key_event(uint8_t key_id, uint8_t action)
{
    uint8_t data[2] = { key_id, action };
    uint16_t len = proto_build_frame(CMD_KEY_EVENT, tx_seq++, data, 2);
    safe_send(proto_get_tx_buf(), len);
}

/**
 * @brief  发送 LED 状态帧
 * @date   2026-08-07
 */
static void send_led_status(void)
{
    led_state_t state = led_mgr_get_state();
    uint8_t data = (uint8_t)state;
    uint16_t len = proto_build_frame(CMD_LED_STATUS, tx_seq++, &data, 1);
    safe_send(proto_get_tx_buf(), len);
}

/**
 * @brief  发送模式状态帧
 * @date   2026-08-07
 */
static void send_mode_status(void)
{
    /* v3.1: 2字节 [is_remote, sub_mode] */
    uint8_t data[2];
    data[0] = display_mgr_is_remote() ? 1 : 0;
    data[1] = display_mgr_get_sub_mode();
    uint16_t len = proto_build_frame(CMD_MODE_STATUS, tx_seq++, data, 2);
    safe_send(proto_get_tx_buf(), len);
}


