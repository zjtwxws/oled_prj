/**
 * @file    main.c
 * @brief   STM32F407 主程序 — OLED 三级联动项目
 */

#include "stm32f4xx_hal.h"
#include <string.h>

#include "i2c_drv.h"
#include "ssd1306.h"
#include "uart_drv.h"
#include "protocol.h"
#include "display_mgr.h"
#include "led_mgr.h"
#include "key_drv.h"
#include "iwdg_drv.h"
#include "sys_config.h"
#include "debug_console.h"

extern I2C_HandleTypeDef  hi2c2;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;

static void SystemClock_Config(void);
static void process_frame(const proto_frame_t *frame);
static void send_ack(uint8_t seq);
static void send_nak(uint8_t seq, uint8_t error_code);
static void send_key_event(uint8_t key_id, uint8_t action);
static void send_led_status(void);
static void send_mode_status(void);

static volatile uint8_t tx_seq = 0;

/*
 * 发送临界区: proto_build_frame 使用全局 tx_buf,
 * 在 build → send 之间需关中断防止 ISR 中覆盖。
 * 注意: 关中断时间极短 (微秒级), 不影响实时性。
 */
static void safe_send(const uint8_t *data, uint16_t len)
{
    __disable_irq();
    uart_drv_send(data, len);
    __enable_irq();
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_USART1_UART_Init();
    MX_USART2_UART_Init();
    MX_I2C2_Init();

    i2c_drv_init(&hi2c2);
    uart_drv_init(&huart1);
    debug_console_init(&huart2);

    ssd1306_init();
    sys_config_init();

    led_mgr_init();
    key_drv_init();
    iwdg_drv_init();

    DEBUG_PRINTF("SYSTEM: Boot complete, entering main loop");

    display_mgr_init(sys_config_get_boot_text());

    uint32_t last_tick = HAL_GetTick();
    uint32_t last_key_scan = 0;
    uint32_t led_tick_acc = 0;
    key_info_t key_info;
    uint8_t rx_byte;

    while (1)
    {
        /*
         * 协议帧间超时检测: 若超过 PROTO_RX_TIMEOUT_MS 未收到完整帧,
         * 自动复位接收状态机防止卡死。
         */
        if (proto_get_last_byte_tick() > 0 &&
            HAL_GetTick() - proto_get_last_byte_tick() > PROTO_RX_TIMEOUT_MS) {
            proto_reset_rx();
        }

        while (uart_drv_available()) {
            uart_drv_read_byte(&rx_byte);
            if (proto_feed_byte(rx_byte)) {
                const proto_frame_t *f = proto_get_frame();
                DEBUG_PRINTF("RX: cmd=0x%02X seq=%d len=%d", f->cmd, f->seq, f->len);
                process_frame(f);
            }
        }

        if (HAL_GetTick() - last_key_scan >= 20) {
            last_key_scan = HAL_GetTick();
            if (key_drv_scan(&key_info)) {
                DEBUG_PRINTF("KEY: id=%d event=%d", key_info.key_id, key_info.event);
                switch (key_info.key_id) {
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
                }
                send_key_event(key_info.key_id, key_info.event);
            }
        }

        {
            uint32_t now = HAL_GetTick();
            uint32_t elapsed = now - last_tick;
            last_tick = now;
            led_tick_acc += elapsed;
            if (led_tick_acc >= 50) {
                led_mgr_tick(led_tick_acc);
                led_tick_acc = 0;
            }
        }

        {
            static uint32_t disp_tick = 0;
            uint32_t now = HAL_GetTick();
            if (now - disp_tick >= 50) {
                disp_tick = now;
                display_mgr_tick();
            }
        }

        iwdg_drv_feed();
    }
}

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 8;
    RCC_OscInitStruct.PLL.PLLN       = 336;
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ       = 7;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) { while(1); }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) { while(1); }
}

static void process_frame(const proto_frame_t *frame)
{
    switch (frame->cmd) {

    case CMD_LED_CTRL:
        if (frame->len >= 1 && frame->data[0] <= 2) {
            DEBUG_PRINTF("LED: set state=%d", frame->data[0]);
            led_mgr_set_state((led_state_t)frame->data[0]);
            send_ack(frame->seq);
            send_led_status();
        } else {
            send_nak(frame->seq, NAK_PARAM_ERROR);
        }
        break;

    case CMD_DISPLAY_MODE:
        if (frame->len >= 1 && frame->data[0] < DISP_MODE_COUNT) {
            DEBUG_PRINTF("DISP: set mode=%d", frame->data[0]);
            display_mgr_set_mode((display_mode_t)frame->data[0]);
            send_ack(frame->seq);
            send_mode_status();
        } else {
            send_nak(frame->seq, NAK_PARAM_ERROR);
        }
        break;

    case CMD_TEXT_CONTENT:
        if (frame->len > 0 && frame->len <= PROTO_MAX_DATA) {
            DEBUG_PRINTF("TEXT: len=%d", frame->len);
            char buf[PROTO_MAX_DATA + 1];
            memcpy(buf, frame->data, frame->len);
            buf[frame->len] = '\0';
            display_mgr_set_text(buf);
            send_ack(frame->seq);
        } else {
            send_nak(frame->seq, NAK_PARAM_ERROR);
        }
        break;

    case CMD_TIME_SYNC:
        if (frame->len >= 7) {
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
        } else {
            send_nak(frame->seq, NAK_PARAM_ERROR);
        }
        break;

    case CMD_WEATHER_DATA:
        if (frame->len >= 4) {
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
        } else {
            send_nak(frame->seq, NAK_PARAM_ERROR);
        }
        break;

    case CMD_BOOT_TEXT:
        if (frame->len > 0) {
            DEBUG_PRINTF("BOOT: text len=%d", frame->len);
            char buf[PROTO_MAX_DATA + 1];
            memcpy(buf, frame->data, frame->len);
            buf[frame->len] = '\0';
            sys_config_set_boot_text(buf);
            display_mgr_set_boot_text(buf);
            send_ack(frame->seq);
        } else {
            send_nak(frame->seq, NAK_PARAM_ERROR);
        }
        break;

    case CMD_ACK:
        break;

    case CMD_NAK:
        if (frame->len >= 1) {
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
 * 之间被 ISR (如 display_mgr_sync_frame) 覆盖全局 tx_buf。
 */
static void send_ack(uint8_t seq)
{
    uint16_t len = proto_build_frame(CMD_ACK, seq, NULL, 0);
    safe_send(proto_get_tx_buf(), len);
}

static void send_nak(uint8_t seq, uint8_t error_code)
{
    uint16_t len = proto_build_frame(CMD_NAK, seq, &error_code, 1);
    safe_send(proto_get_tx_buf(), len);
}

static void send_key_event(uint8_t key_id, uint8_t action)
{
    uint8_t data[2] = { key_id, action };
    uint16_t len = proto_build_frame(CMD_KEY_EVENT, tx_seq++, data, 2);
    safe_send(proto_get_tx_buf(), len);
}

static void send_led_status(void)
{
    uint8_t data = (uint8_t)led_mgr_get_state();
    uint16_t len = proto_build_frame(CMD_LED_STATUS, tx_seq++, &data, 1);
    safe_send(proto_get_tx_buf(), len);
}

static void send_mode_status(void)
{
    uint8_t data = (uint8_t)display_mgr_get_mode();
    uint16_t len = proto_build_frame(CMD_MODE_STATUS, tx_seq++, &data, 1);
    safe_send(proto_get_tx_buf(), len);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1);
}
