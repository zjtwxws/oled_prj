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
#include "iwdg_drv.h"
#include "sys_config.h"
#include "sys_tick.h"
#include "debug_console.h"
#include "cli_cmds.h"
#include "menu_mgr.h"

/* 外设定义 — HAL 句柄类型前向声明 (定义于 CubeMX 生成的 main.c) */
typedef struct I2C_HandleTypeDef  I2C_HandleTypeDef;
typedef struct UART_HandleTypeDef UART_HandleTypeDef;
extern I2C_HandleTypeDef  hi2c2;  /* I2C2: SSD1306 OLED 通信 */
extern UART_HandleTypeDef huart1; /* USART1: PC 上位机通信 */
extern UART_HandleTypeDef huart2; /* USART2: 调试打印信息 */

static void process_frame(const proto_frame_t *frame);
static void send_ack(uint8_t seq);
static void send_nak(uint8_t seq, uint8_t error_code);
static void send_key_event(uint8_t key_id, uint8_t action);
static void send_led_status(void);
static void send_mode_status(void);

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
    iwdg_drv_init();
    
    display_mgr_init(sys_config_get_boot_text());
    /* 上电画面保持 2s，让用户看清启动内容 */
    sys_tick_delay_ms(2000);
    menu_mgr_init();
    
    DEBUG_PRINTF("Firmware Version: " FW_VERSION);
    DEBUG_PRINTF("SYSTEM: Boot complete, entering main loop");

    return 0;
}

//需要在main里面的while(1)里面调用
/**
 * @brief  主循环处理（协议解析/按键扫描/显示/看门狗）
 * @date   2026-08-07
 */
int user_app_handle(void)
{
    static uint32_t last_tick = 0;
    static uint32_t last_key_scan = 0;
    static uint32_t led_tick_acc = 0;
    cli_poll();

    uint32_t bytes_read_this_loop = 0;
    key_info_t key_info;
    uint8_t rx_byte;

    /*
     * 协议帧间超时检测: 若超过 PROTO_RX_TIMEOUT_MS 未收到完整帧,
     * 自动复位接收状态机防止卡死。
     */
    if (proto_get_last_byte_tick() > 0 &&
        sys_tick_ms() - proto_get_last_byte_tick() > PROTO_RX_TIMEOUT_MS)
    {
        proto_reset_rx();
    }

    while (uart_drv_available())
    {
        uart_drv_read_byte(&rx_byte);
        bytes_read_this_loop++;
        if (proto_feed_byte(rx_byte))
        {
            const proto_frame_t *f = proto_get_frame();
            DEBUG_PRINTF("RX: cmd=0x%02X seq=%d len=%d", f->cmd, f->seq, f->len);
            process_frame(f);
        }
    }

    /* 远程模式超时提示: 5秒无串口数据则在OLED显示"串口已断开" */
    {
        static uint32_t last_rx_tick = 0;
        static bool     disconnect_shown = false;
        if (bytes_read_this_loop > 0)
        {
            last_rx_tick = sys_tick_ms();
            if (disconnect_shown)
            {
                disconnect_shown = false;
                display_mgr_hide_disconnect();
            }
        }
        if (!menu_mgr_is_active() && display_mgr_is_remote() && last_rx_tick > 0 &&
            sys_tick_ms() - last_rx_tick > 5000 && !disconnect_shown)
        {
            disconnect_shown = true;
            DEBUG_PRINTF("MODE: serial disconnected");
            display_mgr_show_disconnect();
        }
    }

    if (sys_tick_ms() - last_key_scan >= 20)
    {
        last_key_scan = sys_tick_ms();
        if (key_drv_scan(&key_info))
        {
            DEBUG_PRINTF("KEY: id=%d event=%d", key_info.key_id, key_info.event);
            if (menu_mgr_is_active())
            {
                /* 菜单激活: 按键全部交给 menu_mgr */
                menu_mgr_handle_key(key_info.key_id, key_info.event);
            }
            else
            {
                /* 正常显示: 原有按键逻辑 */
                switch (key_info.key_id)
                {
                case 1:
                    display_mgr_next_mode();
                    send_mode_status();
                    break;
                case 2:
                    {
                        uint8_t next = (led_mgr_get_state() + 1) % 3;
                        led_mgr_set_state((led_state_t)next);
                        send_led_status();
                    }
                    break;
                case 4:
                    /* KEY4 长按 → 激活菜单 */
                    if (key_info.event == KEY_EVENT_LONG_PRESS)
                    {
                        menu_mgr_activate();
                    }
                    break;
                }
            }
            /* REPEAT 事件不上报上位机 */
            if (key_info.event != KEY_EVENT_LONG_PRESS_REPEAT)
            {
                send_key_event(key_info.key_id, key_info.event);
            }
        }
    }

    {
        uint32_t now = sys_tick_ms();
        uint32_t elapsed = now - last_tick;
        last_tick = now;
        led_tick_acc += elapsed;
        if (led_tick_acc >= 50)
        {
            led_mgr_tick(led_tick_acc);
            led_tick_acc = 0;
        }
    }

    {
        static uint32_t disp_tick = 0;
        uint32_t now = sys_tick_ms();
        if (now - disp_tick >= 50)
        {
            disp_tick = now;
            if (menu_mgr_is_active())
            {
                menu_mgr_tick();
            }
            else
            {
                display_mgr_tick();
            }
        }
    }

    iwdg_drv_feed();

    return 0;
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
        if (frame->len >= 1 && frame->data[0] <= 2)
        {
            DEBUG_PRINTF("LED: set state=%d", frame->data[0]);
            led_mgr_set_state((led_state_t)frame->data[0]);
            send_ack(frame->seq);
            send_led_status();
        }
        else
        {
            send_nak(frame->seq, NAK_PARAM_ERROR);
        }
        break;

    case CMD_DISPLAY_MODE:
        if (frame->len >= 1)
        {
            /*
             * data[0]: bit7=0 本地, bit7=1 远程; 低7位=子模式
             * 本地模式: 低7位=特效号
             * 远程模式: 低7位=子模式 (0=TEXT,1=TIME,2=WEATHER,3=DATE)
             */
            bool to_remote = (frame->data[0] & 0x80) != 0;
            uint8_t sub = frame->data[0] & 0x7F;

            DEBUG_PRINTF("DISP: to=%s sub=%d",
                         to_remote ? "remote" : "local", sub);

            display_mgr_set_remote(to_remote);

            if (to_remote)
            {
                if (sub <= REMOTE_SUB_DATE)
                    display_mgr_set_sub_mode(sub);
                /* 远程模式下 EFFECT 切换通过 CMD_DISPLAY_MODE bit7=0 实现 */
            }
            else
            {
                if (sub < DISP_MODE_COUNT)
                    display_mgr_set_mode((display_mode_t)sub);
            }

            send_ack(frame->seq);
            send_mode_status();
        }
        else
        {
            send_nak(frame->seq, NAK_PARAM_ERROR);
        }
        break;

    case CMD_TEXT_CONTENT:
        if (frame->len > 0 && frame->len <= PROTO_MAX_DATA)
        {
            DEBUG_PRINTF("TEXT: len=%d", frame->len);
            char buf[PROTO_MAX_DATA + 1];
            memcpy(buf, frame->data, frame->len);
            buf[frame->len] = '\0';
            display_mgr_set_text(buf);
            send_ack(frame->seq);
        }
        else
        {
            send_nak(frame->seq, NAK_PARAM_ERROR);
        }
        break;

    case CMD_TIME_SYNC:
        if (frame->len >= 7)
        {
            DEBUG_PRINTF("TIME: 20%02d-%02d-%02d %02d:%02d:%02d wd=%d",
                         frame->data[0], frame->data[1], frame->data[2],
                         frame->data[3], frame->data[4], frame->data[5],
                         frame->data[6]);
            display_status_t st = *display_mgr_get_status();
            st.year     = frame->data[0];
            st.month    = frame->data[1];
            st.day      = frame->data[2];
            st.hour     = frame->data[3];
            st.minute   = frame->data[4];
            st.second   = frame->data[5];
            st.week_day = frame->data[6];
            display_mgr_update_status(&st);
            send_ack(frame->seq);
        }
        else
        {
            send_nak(frame->seq, NAK_PARAM_ERROR);
        }
        break;

    case CMD_WEATHER_DATA:
        if (frame->len >= 4)
        {
            DEBUG_PRINTF("WTHR: type=%d temp=%d hum=%d wind=%d",
                         frame->data[0], (int8_t)frame->data[1],
                         frame->data[2], frame->data[3]);
            display_status_t st = *display_mgr_get_status();
            st.weather_type = frame->data[0];
            st.temperature  = (int8_t)frame->data[1];
            st.humidity     = frame->data[2];
            st.wind_dir     = frame->data[3];
            display_mgr_update_status(&st);
            send_ack(frame->seq);
        }
        else
        {
            send_nak(frame->seq, NAK_PARAM_ERROR);
        }
        break;

    case CMD_FRAME_SYNC:
        /* 仅远程模式处理帧缓冲分段 */
        if (!display_mgr_is_remote())
        {
            DEBUG_PRINTF("FRAME: ignored (local mode)");
            send_nak(frame->seq, NAK_BUSY);
            break;
        }
        if (frame->len >= 2)
        {
            uint8_t seg   = frame->data[0];
            uint8_t total = frame->data[1];
            uint8_t payload_len = frame->len - 2;
            DEBUG_PRINTF("FRAME: seg=%d/%d len=%d", seg, total, payload_len);
            display_mgr_rx_frame_seg(seg, total, frame->data + 2, payload_len);
            send_ack(frame->seq);
        }
        else
        {
            send_nak(frame->seq, NAK_PARAM_ERROR);
        }
        break;

    case CMD_BOOT_TEXT:
        if (frame->len > 0)
        {
            DEBUG_PRINTF("BOOT: text len=%d", frame->len);
            char buf[PROTO_MAX_DATA + 1];
            memcpy(buf, frame->data, frame->len);
            buf[frame->len] = '\0';
            sys_config_set_boot_text(buf);
            display_mgr_set_boot_text(buf);
            send_ack(frame->seq);
        }
        else
        {
            send_nak(frame->seq, NAK_PARAM_ERROR);
        }
        break;

    case CMD_ACK:
        break;

    case CMD_NAK:
        if (frame->len >= 1)
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

