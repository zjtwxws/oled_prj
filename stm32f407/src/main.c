/**
 * @file    main.c
 * @brief   STM32F407 主程序 — OLED 三级联动项目
 *
 * 主循环:
 *   1. 处理 UART 接收的协议帧 → 执行命令 (LED/显示/文字/天气/时间/启动文字)
 *   2. 扫描按键 → 上报事件
 *   3. 刷新显示管理器 tick → 驱动特效
 *   4. 喂狗
 *
 * HAL 初始化:
 *   MX_GPIO_Init, MX_USARTx_UART_Init, MX_I2Cx_Init
 *   由 CubeMX 生成的 main.c 中复制过来即可.
 */

#include "stm32f4xx_hal.h"
#include <string.h>

/* 项目模块 */
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

/* ---- 外部 HAL 句柄 (由 CubeMX 生成, 需根据实际配置修改) ---- */
extern I2C_HandleTypeDef  hi2c1;
extern UART_HandleTypeDef huart1;  /* 与 RK3506 通信的串口 */
extern UART_HandleTypeDef huart2;  /* 调试串口 (需在 CubeMX 中配置 USART2, 仅 TX) */

/* ---- 私有函数声明 ---- */
static void SystemClock_Config(void);
static void process_frame(const proto_frame_t *frame);
static void send_ack(uint8_t seq);
static void send_nak(uint8_t seq, uint8_t error_code);
static void send_key_event(uint8_t key_id, uint8_t action);
static void send_led_status(void);
static void send_mode_status(void);

/* 全局序号 */
static volatile uint8_t tx_seq = 0;

/* ---- 主程序 ---- */

int main(void)
{
    /* HAL 初始化 */
    HAL_Init();
    SystemClock_Config();

    /* CubeMX 生成的外设初始化 (函数名/参数以实际 CubeMX 工程为准) */
    MX_GPIO_Init();
    MX_USART1_UART_Init();
    MX_USART2_UART_Init();   /* 调试串口 */
    MX_I2C1_Init();

    /* 驱动层初始化 */
    i2c_drv_init(&hi2c1);
    uart_drv_init(&huart1);
    debug_console_init(&huart2);  /* 调试串口初始化 */

    /* OLED 初始化 */
    ssd1306_init();

    /* 系统配置 (上电文字) */
    sys_config_init();

    /* 应用层初始化 */
    led_mgr_init();
    key_drv_init();
    iwdg_drv_init();

    DEBUG_PRINTF("SYSTEM: Boot complete, entering main loop");

    /* 显示管理器初始化 (使用 Flash 中存储的上电默认文字) */
    display_mgr_init(sys_config_get_boot_text());

    /* 超级循环变量 */
    uint32_t last_tick = HAL_GetTick();
    uint32_t last_key_scan = 0;
    uint32_t led_tick_acc = 0;
    key_info_t key_info;
    uint8_t rx_byte;

    /* 主循环 */
    while (1)
    {
        /* --- UART 帧接收处理 --- */
        while (uart_drv_available()) {
            uart_drv_read_byte(&rx_byte);
            if (proto_feed_byte(rx_byte)) {
                const proto_frame_t *f = proto_get_frame();
                DEBUG_PRINTF("RX: cmd=0x%02X seq=%d len=%d", f->cmd, f->seq, f->len);
                process_frame(f);
            }
        }

        /* --- 按键扫描 (每 20ms) --- */
        if (HAL_GetTick() - last_key_scan >= 20) {
            last_key_scan = HAL_GetTick();
            if (key_drv_scan(&key_info)) {
                DEBUG_PRINTF("KEY: id=%d event=%d", key_info.key_id, key_info.event);
                /* 处理按键事件 */
                switch (key_info.key_id) {
                case 1:  /* KEY1: 切换显示模式 */
                    display_mgr_next_mode();
                    send_mode_status();
                    break;
                case 2:  /* KEY2: 切换 LED */
                    {
                        uint8_t next = (led_mgr_get_state() + 1) % 3;
                        led_mgr_set_state((led_state_t)next);
                        send_led_status();
                    }
                    break;
                }
                /* 上报按键事件 */
                send_key_event(key_info.key_id, key_info.event);
            }
        }

        /* --- LED 闪烁 tick --- */
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

        /* --- 显示管理器 tick (每 50ms 驱动特效) --- */
        {
            static uint32_t disp_tick = 0;
            uint32_t now = HAL_GetTick();
            if (now - disp_tick >= 50) {
                disp_tick = now;
                display_mgr_tick();
            }
        }

        /* --- 喂狗 --- */
        iwdg_drv_feed();
    }
}

/* ---- 系统时钟配置 (需根据实际晶振调整) ---- */
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

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        while(1);
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
        while(1);
    }
}

/* ---- 协议处理 ---- */

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
            DEBUG_PRINTF("LED: param error (len=%d val=%d)", frame->len, frame->data[0]);
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
            DEBUG_PRINTF("DISP: param error (len=%d val=%d)", frame->len, frame->data[0]);
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
            DEBUG_PRINTF("TEXT: param error (len=%d)", frame->len);
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
            DEBUG_PRINTF("TIME: param error (len=%d)", frame->len);
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
            DEBUG_PRINTF("WTHR: param error (len=%d)", frame->len);
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
            DEBUG_PRINTF("BOOT: param error (len=%d)", frame->len);
            send_nak(frame->seq, NAK_PARAM_ERROR);
        }
        break;

    case CMD_ACK:
        /* RK3506 回复的 ACK, 暂不处理 (仅清除重传计时器) */
        break;

    case CMD_NAK:
        /* RK3506 回复的 NAK, 暂不处理 */
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

static void send_ack(uint8_t seq)
{
    uint16_t len = proto_build_frame(CMD_ACK, seq, NULL, 0);
    uart_drv_send(proto_get_tx_buf(), len);
}

static void send_nak(uint8_t seq, uint8_t error_code)
{
    uint16_t len = proto_build_frame(CMD_NAK, seq, &error_code, 1);
    uart_drv_send(proto_get_tx_buf(), len);
}

static void send_key_event(uint8_t key_id, uint8_t action)
{
    uint8_t data[2] = { key_id, action };
    uint16_t len = proto_build_frame(CMD_KEY_EVENT, tx_seq++, data, 2);
    uart_drv_send(proto_get_tx_buf(), len);
}

static void send_led_status(void)
{
    uint8_t data = (uint8_t)led_mgr_get_state();
    uint16_t len = proto_build_frame(CMD_LED_STATUS, tx_seq++, &data, 1);
    uart_drv_send(proto_get_tx_buf(), len);
}

static void send_mode_status(void)
{
    uint8_t data = (uint8_t)display_mgr_get_mode();
    uint16_t len = proto_build_frame(CMD_MODE_STATUS, tx_seq++, &data, 1);
    uart_drv_send(proto_get_tx_buf(), len);
}

/* ---- HAL 中断回调 ----
 * HAL_UART_RxCpltCallback 已移入 uart_drv.c，避免 DR 双重读取。
 * 原因: HAL_UART_Receive_IT 在中断内已将 DR 读入 rx_byte 缓冲区后才
 *       触发此回调，若此处再次读取 DR 将导致数据错乱。
 * 请确保 CubeMX 生成的 stm32f4xx_it.c 不包含同名的弱定义回调。
 */

/* 硬件错误处理 */
void Error_Handler(void)
{
    __disable_irq();
    while (1);
}
