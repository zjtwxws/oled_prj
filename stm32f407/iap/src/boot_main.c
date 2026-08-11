/**
 * @file    boot_main.c
 * @brief   Bootloader 主入口 — 基于 CubeMX 硬件初始化 + 启动决策 / OTA / APP 跳转
 *
 * 编译条件: KEIL 下预定义 STM32F407xx, USE_HAL_DRIVER, BOOTLOADER
 * 链接地址: ROM=0x08000000 RAM=0x20000000
 *
 * 硬件初始化复用 oled_cubemx CubeMX 生成代码:
 *   HAL_Init → SystemClock_Config → MX_GPIO_Init → MX_USART2_UART_Init
 *
 * 进入固件更新模式的条件:
 *   1. KEY1(PE1) + KEY2(PE2) 同时长按 >= 3s
 *   2. APP 设置 ota_request 标志后复位
 *   3. 两个 APP 槽均无效 (全新芯片或固件损坏)
 */

#include "main.h"
#include "gpio.h"
#include "usart.h"
#include "stm32f4xx_it.h"
#include "boot_fw_info.h"
#include "boot_flash.h"
#include "boot_proto.h"
#include "boot_oled.h"
#include <string.h>
#include <stdarg.h>

/* ---- 调试串口开关 (生产版本注释此行) ---- */
#define BOOT_DEBUG_ENABLE

/* ---- 常量 ---- */
#define LED_BLINK_FAST_MS  150
#define LED_BLINK_SLOW_MS  500
#define UART_RX_TIMEOUT_MS 1
#define SLOT_A_BASE        0x08008000UL
#define SLOT_B_BASE        0x08060000UL
#define DEBUG_PRINTF_BUF   128

/* ---- 前向声明 ---- */
void SystemClock_Config(void);
static void uart_send(const uint8_t *data, uint16_t len);
static int  uart_recv_byte(uint8_t *ch);
static void delay_ms(uint32_t ms);
static void led_set(uint8_t on);
static void led_blink(int count, uint32_t period_ms);
static void boot_jump_to_app(uint32_t app_base);
static void enter_update_mode(void);
static uint32_t get_slot_base(uint8_t slot);

#ifdef BOOT_DEBUG_ENABLE
static void boot_printf(const char *fmt, ...);
#define BOOT_LOG(fmt, ...)  boot_log_impl(__func__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define BOOT_LOG(fmt, ...)  ((void)0)
#endif

/* ================================================================
 *  自定义 vsnprintf — 避免 ARMCC semihosting 在无调试器时挂死
 *  支持格式: %s, %d, %u, %x, %X, %02X, %08X, %%
 * ================================================================ */
#ifdef BOOT_DEBUG_ENABLE
static int boot_vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
    char *dst = buf;
    char *end = buf + size - 1;

    if (size == 0)
    {
        return 0;
    }

    while (*fmt && dst < end)
    {
        if (*fmt != '%')
        {
            *dst++ = *fmt++;
            continue;
        }

        fmt++;

        int width = 0;
        char pad = ' ';
        if (*fmt == '0')
        {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9')
        {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        switch (*fmt)
        {
        case 's':
        {
            const char *s = va_arg(args, const char *);
            if (s == NULL) { s = "(null)"; }
            while (*s && dst < end) { *dst++ = *s++; }
            break;
        }
        case 'd':
        {
            int32_t val = va_arg(args, int32_t);
            if (val < 0) { *dst++ = '-'; val = -val; }
            goto do_unsigned;
        }
        case 'u':
        {
            uint32_t val = va_arg(args, uint32_t);
do_unsigned:
        {
            char tmp[16];
            int pos = 15;
            tmp[15] = '\0';
            do
            {
                tmp[--pos] = (char)('0' + (val % 10));
                val /= 10;
            }
            while (val > 0 && pos > 0);

            int num_digits = 15 - pos;
            int pad_count = (width > num_digits) ? (width - num_digits) : 0;
            while (pad_count > 0 && dst < end) { *dst++ = pad; pad_count--; }
            while (tmp[pos] && dst < end) { *dst++ = tmp[pos++]; }
        }
            break;
        }
        case 'x':
        case 'X':
        {
            uint32_t val = va_arg(args, uint32_t);
            char tmp[16];
            int pos = 15;
            tmp[15] = '\0';
            char hex_base = (char)((*fmt == 'X') ? 'A' : 'a');

            do
            {
                uint32_t digit = val & 0x0F;
                tmp[--pos] = (char)((digit < 10) ? ('0' + digit) : (hex_base + digit - 10));
                val >>= 4;
            }
            while (val > 0 && pos > 0);

            int num_digits = 15 - pos;
            int pad_count = (width > num_digits) ? (width - num_digits) : 0;
            while (pad_count > 0 && dst < end) { *dst++ = pad; pad_count--; }
            while (tmp[pos] && dst < end) { *dst++ = tmp[pos++]; }
            break;
        }
        case '%':
            *dst++ = '%';
            break;
        default:
            *dst++ = *fmt;
            break;
        }
        fmt++;
    }

    *dst = '\0';
    return (int)(dst - buf);
}
#endif

/* ================================================================
 *  SystemClock_Config — 严格匹配 CubeMX 生成代码
 * ================================================================ */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 4;
    RCC_OscInitStruct.PLL.PLLN = 168;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 4;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
    {
        Error_Handler();
    }
}

/* ================================================================
 *  Error_Handler — HAL 库错误回调
 * ================================================================ */
void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}

/* ---- 外设操作 ---- */

static void uart_send(const uint8_t *data, uint16_t len)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)data, len, 100);
}

static int uart_recv_byte(uint8_t *ch)
{
    return (HAL_UART_Receive(&huart1, ch, 1, UART_RX_TIMEOUT_MS) == HAL_OK);
}

#ifdef BOOT_DEBUG_ENABLE
static void boot_printf(const char *fmt, ...)
{
    char buf[DEBUG_PRINTF_BUF];
    va_list args;
    va_start(args, fmt);
    int len = boot_vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0)
    {
        HAL_UART_Transmit(&huart2, (uint8_t *)buf, (uint16_t)len, 100);
    }
}

/**
 * @brief  BOOT 调试日志 — __func__ 和 __LINE__ 作为固定参数，避免 va_list 可变参数取值异常
 */
static void boot_log_impl(const char *func, int line, const char *fmt, ...)
{
    char buf[DEBUG_PRINTF_BUF];
    int len = 0;
    const char *p;

    p = "[BOOT] ";
    while (*p && len < DEBUG_PRINTF_BUF - 1) { buf[len++] = *p++; }
    while (*func && len < DEBUG_PRINTF_BUF - 1) { buf[len++] = *func++; }
    if (len < DEBUG_PRINTF_BUF - 1) { buf[len++] = ':'; }

    {
        char ln[8];
        int v = line;
        int i = 7;
        ln[7] = '\0';
        do { ln[--i] = (char)('0' + (v % 10)); v /= 10; } while (v > 0 && i > 0);
        while (i < 7 && len < DEBUG_PRINTF_BUF - 1) { buf[len++] = ln[i++]; }
    }

    if (len < DEBUG_PRINTF_BUF - 1) { buf[len++] = ' '; }

    va_list args;
    va_start(args, fmt);
    int remain = DEBUG_PRINTF_BUF - len - 3;
    int msg_len = boot_vsnprintf(buf + len, (remain > 0) ? (size_t)remain : 0, fmt, args);
    va_end(args);
    if (msg_len > 0) { len += msg_len; }

    if (len < DEBUG_PRINTF_BUF - 1) { buf[len++] = '\r'; }
    if (len < DEBUG_PRINTF_BUF - 1) { buf[len++] = '\n'; }
    buf[len] = '\0';

    if (len > 0) { HAL_UART_Transmit(&huart2, (uint8_t *)buf, (uint16_t)len, 100); }
}

#endif

/* ---- 延时 (基于指令循环, 168MHz 约 42000 循环 = 1ms) ---- */

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

/* ---- LED 控制 (PF9, 低电平亮) ---- */

static void led_set(uint8_t on)
{
    HAL_GPIO_WritePin(USER_LED0_GPIO_Port, USER_LED0_Pin,
                      on ? GPIO_PIN_RESET : GPIO_PIN_SET);
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



static uint32_t get_slot_base(uint8_t slot)
{
    return (slot == 0) ? SLOT_A_BASE : SLOT_B_BASE;
}

/* ---- APP 跳转 ---- */

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

    __HAL_RCC_USART2_CLK_DISABLE();

   __set_MSP(app_sp);
   SCB->VTOR = app_base;

    __enable_irq();

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
    boot_oled_status("进入升级模式");
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

                BOOT_LOG("CMD_OTA_START: slot=0x%08X size=%u crc=0x%08X ver=0x%08X",
                         get_slot_base(target_slot), fw_size, fw_crc32, fw_version);

                if (target_slot > 1)
                {
                    BOOT_LOG("CMD_OTA_START: invalid slot 0x%08X", get_slot_base(target_slot));
                    nak_code = NAK_PARAM_ERROR;
                    break;
                }

                BOOT_LOG("erasing slot 0x%08X...", get_slot_base(target_slot));
                boot_oled_status("擦除中...");
                if (boot_erase_slot(target_slot) != 0)
                {
                    BOOT_LOG("erase slot 0x%08X FAILED", get_slot_base(target_slot));
                    nak_code = NAK_OTA_ERASE;
                    break;
                }
                BOOT_LOG("erase slot 0x%08X OK", get_slot_base(target_slot));

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
                    boot_oled_progress(bytes_written, fw_size);
                }

                goto send_ack;
            }
            else if (cmd == CMD_OTA_FINISH)
            {
                BOOT_LOG("CMD_OTA_FINISH: verifying CRC32...");
                boot_oled_status("校验中...");
                uint32_t calc_crc = boot_crc32_slot(target_slot, fw_size);
                BOOT_LOG("CRC32: expected=0x%08X calculated=0x%08X", fw_crc32, calc_crc);

                if (calc_crc != fw_crc32)
                {
                    BOOT_LOG("CRC32 MISMATCH, update aborted");
                    nak_code = NAK_OTA_CRC;
                    state = UPD_IDLE;
                    break;
                }

                BOOT_LOG("CRC32 OK, activating slot 0x%08X", get_slot_base(target_slot));
                fw_info_set_slot_info(target_slot, fw_size, fw_crc32, fw_version);
                fw_info_set_slot_state(target_slot, SLOT_STATE_VALID);
                fw_info_set_active_slot(target_slot);
                fw_info_clear_ota_request();

                state = UPD_DONE;

                boot_oled_status("升级完成");
                led_set(1);
                delay_ms(1000);
                led_set(0);

                boot_oled_status("启动APP");
                delay_ms(500);

               BOOT_LOG("update complete, jumping to slot 0x%08X",
                        get_slot_base(target_slot));
                tx_len = boot_proto_build(CMD_ACK, NULL, 0);
                uart_send(boot_proto_tx_buf(), tx_len);
                delay_ms(20);
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

/* ================================================================
 *  main — CubeMX 标准初始化 + Bootloader 业务逻辑
 * ================================================================ */
int main(void)
{
    /* ---- CubeMX 标准初始化序列 ---- */
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART2_UART_Init();

    /* ---- OLED 初始化 ---- */
    boot_oled_init();

    MX_USART1_UART_Init();
    /* ---- Bootloader 启动 ---- */
    BOOT_LOG("========================================");
    BOOT_LOG("STM32F407 Bootloader V3.0");
    BOOT_LOG("build: %s %s", __DATE__, __TIME__);
    BOOT_LOG("========================================");

    led_blink(2, LED_BLINK_SLOW_MS);

    if (HAL_GPIO_ReadPin(USER_KEY1_GPIO_Port, USER_KEY1_Pin) == GPIO_PIN_RESET)
    {
        BOOT_LOG("KEY1 pressed, entering update mode");
        enter_update_mode();
    }

    /* 加载固件信息 */
    BOOT_LOG("loading fw_info from S10 (0x%08X)...", FW_INFO_ADDR);
    {
        int ret = fw_info_load();
        BOOT_LOG("fw_info_load: %s", (ret == 0) ? "loaded OK" : "initialized (first boot)");
    }

    const fw_info_t *fi = fw_info_get();
    BOOT_LOG("fw_info: active=0x%08X a_state=0x%02X a_ver=0x%08X b_state=0x%02X b_ver=0x%08X ota_req=%d",
             get_slot_base(fi->active_slot), fi->slot_a_state, fi->slot_a_version,
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

        BOOT_LOG("active slot 0x%08X: state=0x%02X size=%u crc=0x%08X", get_slot_base(active), state, size, crc);

        if (state == SLOT_STATE_VALID && size > 0)
        {
            uint32_t calc_crc = boot_crc32_slot(active, size);
            if (calc_crc == crc)
            {
                BOOT_LOG("active slot 0x%08X CRC OK, jumping...", get_slot_base(active));
                led_blink(1, LED_BLINK_SLOW_MS);
                boot_jump_to_app(get_slot_base(active));
            }
            else
            {
                BOOT_LOG("active slot 0x%08X CRC FAIL: expected=0x%08X got=0x%08X",
                         get_slot_base(active), crc, calc_crc);
            }
        }
        else
        {
            BOOT_LOG("active slot 0x%08X invalid", get_slot_base(active));
        }
    }

    /* 活跃槽无效 -> 尝试另一个槽 */
    {
        uint8_t alt    = (fi->active_slot == 0) ? 1 : 0;
        uint8_t state  = (alt == 0) ? fi->slot_a_state : fi->slot_b_state;
        uint32_t size  = (alt == 0) ? fi->slot_a_size : fi->slot_b_size;
        uint32_t crc   = (alt == 0) ? fi->slot_a_crc  : fi->slot_b_crc;

        BOOT_LOG("trying alternate slot 0x%08X: state=0x%02X size=%u crc=0x%08X",
                 get_slot_base(alt), state, size, crc);

        if (state == SLOT_STATE_VALID && size > 0)
        {
            uint32_t calc_crc = boot_crc32_slot(alt, size);
            if (calc_crc == crc)
            {
                BOOT_LOG("alternate slot 0x%08X CRC OK, switching and jumping...", get_slot_base(alt));
                fw_info_set_active_slot(alt);
                led_blink(2, LED_BLINK_SLOW_MS);
                boot_jump_to_app(get_slot_base(alt));
            }
            else
            {
                BOOT_LOG("alternate slot 0x%08X CRC FAIL", get_slot_base(alt));
            }
        }
    }

    /* 两槽均无效 -> 进入更新模式等待固件 */
    BOOT_LOG("no valid firmware found, entering update mode");
    led_blink(4, LED_BLINK_SLOW_MS);
    enter_update_mode();

    /* unreachable */
}
