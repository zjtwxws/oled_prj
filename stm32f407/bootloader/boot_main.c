/**
 * @file    boot_main.c
 * @brief   Bootloader 主入口 — 启动决策 + OTA 更新状态机 + APP 跳转
 *
 * 编译条件: KEIL 下预定义 STM32F407xx, USE_HAL_DRIVER, BOOTLOADER
 * 链接地址: ROM=0x08000000 RAM=0x20000000
 *
 * 进入固件更新模式的条件:
 *   1. KEY1(PE1) + KEY2(PE2) 同时长按 ≥3s
 *   2. APP 设置 ota_request 标志后复位
 *   3. 两个 APP 槽均无效 (全新芯片或固件损坏)
 */

#include "stm32f4xx_hal.h"
#include "boot_fw_info.h"
#include "boot_flash.h"
#include "boot_proto.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ---- 调试串口开关 (生产版本注释此行) ---- */
#define BOOT_DEBUG_ENABLE

/* ---- 硬件引脚 (与 oled_cubemx 一致) ---- */
#define KEY1_PORT          GPIOE
#define KEY1_PIN           GPIO_PIN_1
#define KEY2_PORT          GPIOE
#define KEY2_PIN           GPIO_PIN_2
#define LED_PORT           GPIOF
#define LED_PIN            GPIO_PIN_9
#define DEBUG_USART        USART2
#define DEBUG_TX_PORT      GPIOA
#define DEBUG_TX_PIN       GPIO_PIN_2
#define DEBUG_RX_PORT      GPIOA
#define DEBUG_RX_PIN       GPIO_PIN_3

/* ---- 常量 ---- */
#define KEY_LONG_PRESS_MS  3000    /* 长按阈值 */
#define LED_BLINK_FAST_MS  150     /* 快速闪烁 (更新模式等待) */
#define LED_BLINK_SLOW_MS  500     /* 慢速闪烁 (正常启动) */
#define UART_RX_TIMEOUT_MS 1       /* 每字节轮询超时 */
#define SLOT_A_BASE        0x08008000UL
#define SLOT_B_BASE        0x08060000UL
#define DEBUG_BAUDRATE     115200
#define DEBUG_PRINTF_BUF   128

/* ---- 全局 ---- */
static UART_HandleTypeDef g_uart1;
static UART_HandleTypeDef g_uart2;

/* ---- 前向声明 ---- */
static void system_clock_config(void);
static void gpio_init(void);
static void uart_init(void);
static void debug_uart_init(void);
static void uart_send(const uint8_t *data, uint16_t len);
static int  uart_recv_byte(uint8_t *ch);
static void delay_ms(uint32_t ms);
static void led_set(uint8_t on);
static void led_blink(int count, uint32_t period_ms);
static int  check_key12_long_press(void);
static void boot_jump_to_app(uint32_t app_base);
static void enter_update_mode(void);
static uint32_t get_slot_base(uint8_t slot);

#ifdef BOOT_DEBUG_ENABLE
static void boot_printf(const char *fmt, ...);
#define BOOT_LOG(fmt, ...)  boot_printf("[BOOT] " fmt "\r\n", ##__VA_ARGS__)
#else
#define BOOT_LOG(fmt, ...)  ((void)0)
#endif

/* ---- 硬件初始化 ---- */

/**
 * @brief  HSE 8MHz → PLL 168MHz → SYSCLK=168MHz, APB1=42MHz, APB2=84MHz
 */
static void system_clock_config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 4;
    osc.PLL.PLLN = 168;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 4;
    HAL_RCC_OscConfig(&osc);

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                  | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5);
}

/**
 * @brief  GPIO 初始化 — KEY1(PE1) + KEY2(PE2) 输入, LED(PF9) 输出
 */
static void gpio_init(void)
{
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();

    GPIO_InitTypeDef init = {0};

    /* KEY1: PE1, KEY2: PE2, 上拉输入 */
    init.Pin = KEY1_PIN | KEY2_PIN;
    init.Mode = GPIO_MODE_INPUT;
    init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(KEY1_PORT, &init);

    /* LED: PF9, 推挽输出 */
    init.Pin = LED_PIN;
    init.Mode = GPIO_MODE_OUTPUT_PP;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &init);

    /* 默认关闭 LED */
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
}

/**
 * @brief  USART1 初始化 — PA9(TX)/PA10(RX), 115200-8-N-1
 */
static void uart_init(void)
{
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA9=TX(AF7), PA10=RX(AF7) */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio);

    g_uart1.Instance = USART1;
    g_uart1.Init.BaudRate = 115200;
    g_uart1.Init.WordLength = UART_WORDLENGTH_8B;
    g_uart1.Init.StopBits = UART_STOPBITS_1;
    g_uart1.Init.Parity = UART_PARITY_NONE;
    g_uart1.Init.Mode = UART_MODE_TX_RX;
    g_uart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    g_uart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&g_uart1);
}

/**
 * @brief  USART2 调试串口 — PA2(TX)/PA3(RX), 115200-8-N-1
 * @note   仅 TX 使用, RX 不使用但需初始化以免浮空
 */
static void debug_uart_init(void)
{
#ifdef BOOT_DEBUG_ENABLE
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA2=TX(AF7), PA3=RX(AF7) */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = DEBUG_TX_PIN | DEBUG_RX_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(DEBUG_TX_PORT, &gpio);

    g_uart2.Instance = DEBUG_USART;
    g_uart2.Init.BaudRate = DEBUG_BAUDRATE;
    g_uart2.Init.WordLength = UART_WORDLENGTH_8B;
    g_uart2.Init.StopBits = UART_STOPBITS_1;
    g_uart2.Init.Parity = UART_PARITY_NONE;
    g_uart2.Init.Mode = UART_MODE_TX_RX;
    g_uart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    g_uart2.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&g_uart2);
#endif
}

/* ---- 外设操作 ---- */

static void uart_send(const uint8_t *data, uint16_t len)
{
    HAL_UART_Transmit(&g_uart1, (uint8_t *)data, len, 100);
}

/**
 * @brief  轮询接收 1 字节
 * @return 1=收到, 0=超时
 */
static int uart_recv_byte(uint8_t *ch)
{
    return (HAL_UART_Receive(&g_uart1, ch, 1, UART_RX_TIMEOUT_MS) == HAL_OK);
}

/**
 * @brief  调试串口格式化输出 (轮询, 阻塞)
 * @note   仅在 BOOT_DEBUG_ENABLE 定义时有效
 */
#ifdef BOOT_DEBUG_ENABLE
static void boot_printf(const char *fmt, ...)
{
    char buf[DEBUG_PRINTF_BUF];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0)
    {
        HAL_UART_Transmit(&g_uart2, (uint8_t *)buf, (uint16_t)len, 100);
    }
}
#endif

/**
 * @brief  简单阻塞延时 (基于指令计数，非 SysTick)
 * @note   168MHz 下约 168000 个循环 = 1ms
 */
static void delay_ms(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++)
    {
        for (volatile uint32_t j = 0; j < 42000; j++)
        {
            __NOP();
        }
    }
}

static void led_set(uint8_t on)
{
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void led_blink(int count, uint32_t period_ms)
{
    for (int i = 0; i < count; i++)
    {
        led_set(1);
        delay_ms(period_ms / 2);
        led_set(0);
        delay_ms(period_ms / 2);
    }
}

/* ---- 启动决策 ---- */

/**
 * @brief  检查 KEY1 + KEY2 是否同时长按 (≥3s)
 * @return 1=双键长按, 0=未满足条件
 */
static int check_key12_long_press(void)
{
    uint32_t press_ms = 0;
    const uint32_t poll_interval = 10;

    BOOT_LOG("KEY1+KEY2 detected, checking long press (%d ms)...", KEY_LONG_PRESS_MS);

    while (press_ms < KEY_LONG_PRESS_MS)
    {
        uint8_t k1 = (HAL_GPIO_ReadPin(KEY1_PORT, KEY1_PIN) == GPIO_PIN_RESET) ? 1 : 0;
        uint8_t k2 = (HAL_GPIO_ReadPin(KEY2_PORT, KEY2_PIN) == GPIO_PIN_RESET) ? 1 : 0;

        if (k1 && k2)
        {
            press_ms += poll_interval;
            delay_ms(poll_interval);
        }
        else
        {
            BOOT_LOG("KEY1+KEY2 released before threshold (%d ms)", press_ms);
            return 0;
        }
    }

    BOOT_LOG("KEY1+KEY2 long press confirmed, entering update mode");
    return 1;
}

static uint32_t get_slot_base(uint8_t slot)
{
    return (slot == 0) ? SLOT_A_BASE : SLOT_B_BASE;
}

/* ---- APP 跳转 ---- */

/**
 * @brief  跳转到指定地址的 APP
 * @param  app_base  APP 向量表基地址 (0x08008000 或 0x08060000)
 */
static void boot_jump_to_app(uint32_t app_base)
{
    uint32_t app_sp = *((volatile uint32_t *)app_base);
    uint32_t app_pc = *((volatile uint32_t *)(app_base + 4));

    if (app_sp < 0x20000000UL || app_sp > 0x2001C000UL)
    {
        BOOT_LOG("jump aborted: invalid SP=0x%08X (slot=0x%08X)", app_sp, app_base);
        return;
    }

    if (app_pc < 0x08000000UL || app_pc > 0x080FFFFFUL)
    {
        BOOT_LOG("jump aborted: invalid PC=0x%08X (slot=0x%08X)", app_pc, app_base);
        return;
    }

    BOOT_LOG("jumping to APP at 0x%08X (SP=0x%08X, PC=0x%08X)", app_base, app_sp, app_pc);

    __disable_irq();

    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    __HAL_RCC_USART1_CLK_DISABLE();
#ifdef BOOT_DEBUG_ENABLE
    __HAL_RCC_USART2_CLK_DISABLE();
#endif

    __set_MSP(app_sp);
    SCB->VTOR = app_base;

    ((void (*)(void))app_pc)();

    while (1) {}
}

/* ---- OTA 更新模式 ---- */

typedef enum
{
    UPD_IDLE = 0,
    UPD_RECEIVING,
    UPD_DONE
} ota_state_t;

static void enter_update_mode(void)
{
    uint8_t      rx_byte;
    ota_state_t  state = UPD_IDLE;
    uint8_t      target_slot;
    uint32_t     fw_size;
    uint32_t     fw_crc32;
    uint32_t     fw_version;
    uint32_t     bytes_written;

    BOOT_LOG("=== UPDATE MODE ENTERED ===");
    led_blink(3, LED_BLINK_FAST_MS);

    while (1)
    {
        if (!uart_recv_byte(&rx_byte))
        {
            continue;
        }

        if (!boot_proto_feed(rx_byte))
        {
            continue;
        }

        const boot_frame_t *frame = boot_proto_get_frame();
        uint8_t cmd  = frame->cmd;
        uint8_t dlen = frame->len;
        const uint8_t *data = frame->data;

        BOOT_LOG("RX frame: cmd=0x%02X len=%d state=%d", cmd, dlen, state);

        uint16_t tx_len;
        uint8_t  nak_code = 0;

        switch (state)
        {
        case UPD_IDLE:
            if (cmd == CMD_OTA_START)
            {
                if (dlen < 13)
                {
                    BOOT_LOG("CMD_OTA_START: param too short (%d)", dlen);
                    nak_code = NAK_PARAM_ERROR;
                    break;
                }
                target_slot = data[0];
                fw_size     = data[1] | ((uint32_t)data[2] << 8)
                            | ((uint32_t)data[3] << 16) | ((uint32_t)data[4] << 24);
                fw_crc32    = data[5] | ((uint32_t)data[6] << 8)
                            | ((uint32_t)data[7] << 16) | ((uint32_t)data[8] << 24);
                fw_version  = data[9] | ((uint32_t)data[10] << 8)
                            | ((uint32_t)data[11] << 16) | ((uint32_t)data[12] << 24);

                BOOT_LOG("CMD_OTA_START: slot=%d size=%u crc=0x%08X ver=0x%08X",
                         target_slot, fw_size, fw_crc32, fw_version);

                if (target_slot > 1)
                {
                    BOOT_LOG("CMD_OTA_START: invalid slot %d", target_slot);
                    nak_code = NAK_PARAM_ERROR;
                    break;
                }

                BOOT_LOG("erasing slot %d...", target_slot);
                if (boot_erase_slot(target_slot) != 0)
                {
                    BOOT_LOG("erase slot %d FAILED", target_slot);
                    nak_code = NAK_OTA_ERASE;
                    break;
                }
                BOOT_LOG("erase slot %d OK", target_slot);

                bytes_written = 0;
                state = UPD_RECEIVING;

                led_blink(1, 50);
                goto send_ack;
            }
            else if (cmd == CMD_OTA_ABORT)
            {
                BOOT_LOG("CMD_OTA_ABORT: exiting update mode");
                return;
            }
            else
            {
                BOOT_LOG("unexpected cmd=0x%02X in IDLE state", cmd);
                nak_code = NAK_UNKNOWN_CMD;
            }
            break;

        case UPD_RECEIVING:
            if (cmd == CMD_OTA_DATA)
            {
                if (dlen < 5)
                {
                    nak_code = NAK_PARAM_ERROR;
                    break;
                }

                uint32_t offset = data[0] | ((uint32_t)data[1] << 8)
                                | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
                uint8_t  plen   = dlen - 4;
                const uint8_t *payload = data + 4;

                uint32_t slot_base = get_slot_base(target_slot);

                if (offset + plen > fw_size)
                {
                    BOOT_LOG("CMD_OTA_DATA: offset %u + plen %u > fw_size %u",
                             offset, plen, fw_size);
                    nak_code = NAK_OTA_OFFSET;
                    break;
                }

                uint32_t flash_addr = slot_base + offset;
                uint32_t word_count = plen / 4;

                for (uint32_t i = 0; i < word_count; i++)
                {
                    uint32_t word = payload[i * 4]
                                  | ((uint32_t)payload[i * 4 + 1] << 8)
                                  | ((uint32_t)payload[i * 4 + 2] << 16)
                                  | ((uint32_t)payload[i * 4 + 3] << 24);
                    if (boot_flash_write_word(flash_addr, word) != 0)
                    {
                        BOOT_LOG("flash write failed at 0x%08X", flash_addr);
                        nak_code = NAK_FLASH_ERROR;
                        goto send_nak;
                    }
                    flash_addr += 4;
                }

                uint8_t remain = plen % 4;
                if (remain > 0)
                {
                    uint32_t last_word = boot_flash_read_word(flash_addr & ~3UL);
                    uint8_t  byte_off  = (offset + plen - remain) & 3;
                    for (uint8_t k = 0; k < remain; k++)
                    {
                        ((uint8_t *)&last_word)[byte_off + k] = payload[word_count * 4 + k];
                    }
                    if (boot_flash_write_word(flash_addr & ~3UL, last_word) != 0)
                    {
                        BOOT_LOG("flash write failed at 0x%08X (tail)", flash_addr & ~3UL);
                        nak_code = NAK_FLASH_ERROR;
                        goto send_nak;
                    }
                }

                bytes_written += plen;

                if (bytes_written % (64 * 1024) < plen + 100)
                {
                    BOOT_LOG("OTA progress: %u / %u bytes (%u%%)",
                             bytes_written, fw_size,
                             (bytes_written * 100) / fw_size);
                    led_blink(1, 30);
                }

                goto send_ack;
            }
            else if (cmd == CMD_OTA_FINISH)
            {
                BOOT_LOG("CMD_OTA_FINISH: verifying CRC32...");
                uint32_t calc_crc = boot_crc32_slot(target_slot, fw_size);
                BOOT_LOG("CRC32: expected=0x%08X calculated=0x%08X", fw_crc32, calc_crc);

                if (calc_crc != fw_crc32)
                {
                    BOOT_LOG("CRC32 MISMATCH, update aborted");
                    nak_code = NAK_OTA_CRC;
                    state = UPD_IDLE;
                    break;
                }

                BOOT_LOG("CRC32 OK, activating slot %d", target_slot);
                fw_info_set_slot_info(target_slot, fw_size, fw_crc32, fw_version);
                fw_info_set_slot_state(target_slot, SLOT_STATE_VALID);
                fw_info_set_active_slot(target_slot);
                fw_info_clear_ota_request();

                state = UPD_DONE;

                led_set(1);
                delay_ms(1000);
                led_set(0);

                BOOT_LOG("update complete, jumping to slot %d (0x%08X)",
                         target_slot, get_slot_base(target_slot));
                boot_jump_to_app(get_slot_base(target_slot));

                BOOT_LOG("jump failed, back to IDLE");
                state = UPD_IDLE;
                break;
            }
            else if (cmd == CMD_OTA_ABORT)
            {
                BOOT_LOG("CMD_OTA_ABORT: cancel update, back to IDLE");
                state = UPD_IDLE;
                goto send_ack;
            }
            else
            {
                nak_code = NAK_UNKNOWN_CMD;
            }
            break;

        case UPD_DONE:
        default:
            nak_code = NAK_BUSY;
            break;
        }

send_nak:
        {
            BOOT_LOG("sending NAK code=0x%02X", nak_code);
            uint8_t nak_data[1] = { nak_code };
            tx_len = boot_proto_build(CMD_NAK, nak_data, 1);
            uart_send(boot_proto_tx_buf(), tx_len);
        }
        continue;

send_ack:
        tx_len = boot_proto_build(CMD_ACK, NULL, 0);
        uart_send(boot_proto_tx_buf(), tx_len);
    }
}

/* ---- 主入口 ---- */

int main(void)
{
    HAL_Init();
    system_clock_config();
    gpio_init();
    uart_init();
    debug_uart_init();

    BOOT_LOG("========================================");
    BOOT_LOG("STM32F407 Bootloader V1.0");
    BOOT_LOG("build: %s %s", __DATE__, __TIME__);
    BOOT_LOG("========================================");

    led_blink(2, LED_BLINK_SLOW_MS);

    /* 检查 KEY1 + KEY2 同时按下 */
    {
        uint8_t k1 = (HAL_GPIO_ReadPin(KEY1_PORT, KEY1_PIN) == GPIO_PIN_RESET) ? 1 : 0;
        uint8_t k2 = (HAL_GPIO_ReadPin(KEY2_PORT, KEY2_PIN) == GPIO_PIN_RESET) ? 1 : 0;
        BOOT_LOG("key check: KEY1=%d KEY2=%d", k1, k2);

        if (k1 && k2)
        {
            if (check_key12_long_press())
            {
                led_blink(3, 50);
                enter_update_mode();
            }
        }
    }

    /* 加载固件信息 */
    BOOT_LOG("loading fw_info from S10 (0x%08X)...", FW_INFO_ADDR);
    {
        int ret = fw_info_load();
        BOOT_LOG("fw_info_load: %s", (ret == 0) ? "loaded OK" : "initialized (first boot)");
    }

    const fw_info_t *fi = fw_info_get();
    BOOT_LOG("fw_info: active=%d a_state=0x%02X a_ver=0x%08X b_state=0x%02X b_ver=0x%08X ota_req=%d",
             fi->active_slot, fi->slot_a_state, fi->slot_a_version,
             fi->slot_b_state, fi->slot_b_version, fi->ota_request);

    /* 检查 ota_request */
    if (fi->ota_request == 1)
    {
        BOOT_LOG("ota_request=1, entering update mode");
        fw_info_clear_ota_request();
        enter_update_mode();
    }

    /* 尝试跳转 active_slot */
    {
        uint8_t active = fi->active_slot;
        uint8_t state  = (active == 0) ? fi->slot_a_state : fi->slot_b_state;
        uint32_t size  = (active == 0) ? fi->slot_a_size : fi->slot_b_size;
        uint32_t crc   = (active == 0) ? fi->slot_a_crc  : fi->slot_b_crc;

        BOOT_LOG("active slot %d: state=0x%02X size=%u crc=0x%08X", active, state, size, crc);

        if (state == SLOT_STATE_VALID && size > 0)
        {
            uint32_t calc_crc = boot_crc32_slot(active, size);
            if (calc_crc == crc)
            {
                BOOT_LOG("active slot %d CRC OK, jumping...", active);
                led_blink(1, LED_BLINK_SLOW_MS);
                boot_jump_to_app(get_slot_base(active));
            }
            else
            {
                BOOT_LOG("active slot %d CRC FAIL: expected=0x%08X got=0x%08X",
                         active, crc, calc_crc);
            }
        }
        else
        {
            BOOT_LOG("active slot %d invalid", active);
        }
    }

    /* 活跃槽无效 → 尝试另一个槽 */
    {
        uint8_t alt    = (fi->active_slot == 0) ? 1 : 0;
        uint8_t state  = (alt == 0) ? fi->slot_a_state : fi->slot_b_state;
        uint32_t size  = (alt == 0) ? fi->slot_a_size : fi->slot_b_size;
        uint32_t crc   = (alt == 0) ? fi->slot_a_crc  : fi->slot_b_crc;

        BOOT_LOG("trying alternate slot %d: state=0x%02X size=%u crc=0x%08X",
                 alt, state, size, crc);

        if (state == SLOT_STATE_VALID && size > 0)
        {
            uint32_t calc_crc = boot_crc32_slot(alt, size);
            if (calc_crc == crc)
            {
                BOOT_LOG("alternate slot %d CRC OK, switching and jumping...", alt);
                fw_info_set_active_slot(alt);
                led_blink(2, LED_BLINK_SLOW_MS);
                boot_jump_to_app(get_slot_base(alt));
            }
            else
            {
                BOOT_LOG("alternate slot %d CRC FAIL", alt);
            }
        }
    }

    /* 两槽均无效 → 进入更新模式等待固件 */
    BOOT_LOG("no valid firmware found, entering update mode");
    enter_update_mode();

    while (1)
    {
        led_blink(1, 1000);
    }
}
