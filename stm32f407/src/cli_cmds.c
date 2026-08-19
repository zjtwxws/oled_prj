/**
 * @file    cli_cmds.c
 * @brief   CLI 调试命令表 — 定义所有可通过串口交互的调试命令
 *
 * 本文件定义 cmd_table[] 和 auto_complete_words[]，供 nr_micro_shell 引擎使用。
 * 新增命令只需在此文件末尾添加条目即可。
 */

#include "nr_micro_shell_port.h"
#include "nr_micro_shell.h"
#include "cli_cmds.h"
#include "display_mgr.h"
#include "led_mgr.h"
#include "app_fw_info.h"
#include "sys_config.h"
#include "sys_tick.h"
#include "user_app.h"
#include "menu_mgr.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- 内置基础命令 ---- */

/**
 * @brief  help — 显示所有可用命令
 */
static int cmd_help(uint8_t argc, char **argv)
{
    (void)argc;
    (void)argv;
    show_all_cmds();
    return 0;
}

/**
 * @brief  clear — 清屏
 */
static int cmd_clear(uint8_t argc, char **argv)
{
    (void)argc;
    (void)argv;
    shell_printf("\033[2J");
    shell_printf("\033[0;0H");
    return 0;
}

/* ---- 系统信息命令 ---- */

/**
 * @brief  info — 显示固件版本和系统信息
 */
static int cmd_info(uint8_t argc, char **argv)
{
    (void)argc;
    (void)argv;

    shell_printf("========================================\r\n");
    shell_printf("  OLED Gateway Debug Console\r\n");
    shell_printf("========================================\r\n");
    shell_printf("  Firmware : %s\r\n", FW_VERSION);
    shell_printf("  Author   : %s\r\n", FW_AUTHOR);
    shell_printf("  Build    : %s\r\n", FW_BUILD_TIME);
    shell_printf("  Uptime   : %lu ms\r\n", sys_tick_ms());
	shell_printf("  Rtos Vern: %s\r\n",FW_RTOS_VERN);

    if (menu_mgr_is_active())
    {
        shell_printf("  State    : Menu Active\r\n");
    }
    else
    {
        shell_printf("  State    : Display (mode=%d, remote=%d)\r\n",
                     display_mgr_get_sub_mode(),
                     display_mgr_is_remote());
    }
    shell_printf("========================================\r\n");

    return 0;
}

/* ---- LED 控制命令 ---- */

/**
 * @brief  led — LED 控制 (led 0=关, led 1=开, led 2=闪烁)
 */
static int cmd_led(uint8_t argc, char **argv)
{
    if (argc < 2)
    {
        shell_printf("Usage: led <0|1|2>\r\n");
        shell_printf("  0: off\r\n");
        shell_printf("  1: on\r\n");
        shell_printf("  2: blink\r\n");
        return -1;
    }

    int state = atoi(argv[1]);
    if (state < 0 || state > 2)
    {
        shell_printf("Error: invalid state %d (must be 0-2)\r\n", state);
        return -1;
    }

    led_mgr_set_state((led_state_t)state);
    shell_printf("LED set to %d\r\n", state);

    return 0;
}

/* ---- 地址合法性校验 ---- */

/**
 * @brief  检查地址是否属于 STM32F407 有效内存/外设区域
 * @param  addr  待检查的地址
 * @return 1=合法, 0=非法 (访问会导致 HardFault)
 * @note   F407VG: Flash 1MB, SRAM 128KB+64KB CCM, 外设区
 */
static int is_valid_address(unsigned long addr)
{
    /* Flash (主存储区) */
    if (addr >= 0x08000000UL && addr <= 0x080FFFFFUL)
    {
        return 1;
    }
    /* SRAM1 (128KB) */
    if (addr >= 0x20000000UL && addr <= 0x2001FFFFUL)
    {
        return 1;
    }
    /* CCM RAM (64KB, 只能 CPU 访问) */
    if (addr >= 0x10000000UL && addr <= 0x1000FFFFUL)
    {
        return 1;
    }
    /* AHB1 外设 (GPIO/RCC/DMA 等) */
    if (addr >= 0x40020000UL && addr <= 0x4007FFFFUL)
    {
        return 1;
    }
    /* APB1 外设 (TIM/I2C/USART/SPI 等) */
    if (addr >= 0x40000000UL && addr <= 0x4000FFFFUL)
    {
        return 1;
    }
    /* APB2 外设 (TIM1/USART1/SPI1/SYSCFG 等) */
    if (addr >= 0x40010000UL && addr <= 0x4001FFFFUL)
    {
        return 1;
    }
    /* AHB2 外设 (OTG/RNG) */
    if (addr >= 0x50000000UL && addr <= 0x500FFFFFUL)
    {
        return 1;
    }
    /* Cortex-M4 系统控制 (NVIC/SCB/SysTick/MPU) */
    if (addr >= 0xE000E000UL && addr <= 0xE00FFFFFUL)
    {
        return 1;
    }
    return 0;
}

/* ---- 内存读取命令 ---- */

/**
 * @brief  rd — 读取内存地址数据 (rd <addr_hex> [size_dec])
 */
static int cmd_rd(uint8_t argc, char **argv)
{
    unsigned long addr;
    uint32_t size = 1;
    uint8_t *p;
    int i;
    int j;

    if (argc < 2)
    {
        shell_printf("Usage: rd <addr_hex> [size_dec, default 1]\r\n");
        shell_printf("Example: rd 0x20000000 16\r\n");
        return -1;
    }

    if (sscanf(argv[1], "%lx", &addr) != 1)
    {
        shell_printf("Invalid address format\r\n");
        return -1;
    }

    if (!is_valid_address(addr))
    {
        shell_printf("Error: address 0x%08lx is not a valid memory region\r\n", addr);
        shell_printf("Valid ranges: Flash(0x08xxxxxx) SRAM(0x2000xxxx) CCM(0x1000xxxx)\r\n");
        shell_printf("              Peripheral(0x400xxxxx) AHB2(0x500xxxxx)\r\n");
        return -1;
    }

    if (argc > 2)
    {
        if (sscanf(argv[2], "%d", &size) != 1)
        {
            shell_printf("Invalid size format\r\n");
            return -1;
        }
    }

    shell_printf("addr: 0x%08lx\r\n", addr);

    p = (uint8_t *)(uintptr_t)addr;
    for (i = 0; i < (int)size; i += 16)
    {
        shell_printf("%08lx: ", (unsigned long)(i));
        for (j = 0; j < 16 && (i + j) < (int)size; j++)
        {
            shell_printf("%02x ", p[i + j]);
            if (j == 7)
            {
                shell_printf(" ");
            }
        }

        while (j < 16)
        {
            shell_printf("   ");
            if (j == 7)
            {
                shell_printf(" ");
            }
            j++;
        }

        shell_printf(" |");

        for (j = 0; j < 16 && (i + j) < (int)size; j++)
        {
            if (p[i + j] >= 32 && p[i + j] <= 126)
            {
                shell_printf("%c", p[i + j]);
            }
            else
            {
                shell_printf(".");
            }
        }

        shell_printf("|\r\n");
    }

    return 0;
}

/* ---- 软件复位命令 ---- */

/**
 * @brief  update — 进入固件更新模式
 */
static int cmd_update(uint8_t argc, char **argv)
{
    (void)argc;
    (void)argv;
    shell_printf("Setting OTA request flag...\r\n");
    if (app_fw_info_set_ota_request() != 0)
    {
        shell_printf("Failed to set OTA flag!\r\n");
        return -1;
    }
    shell_printf("Rebooting to bootloader update mode...\r\n");
    sys_config_reset();
    return 0;
}

/**
 * @brief  reboot — 软件复位 MCU
 */
static int cmd_reboot(uint8_t argc, char **argv)
{
    (void)argc;
    (void)argv;
    shell_printf("Rebooting...\r\n");
    sys_config_reset();
    return 0;
}

/* ---- 显示模式命令 ---- */

/**
 * @brief  mode — 查看/切换显示模式 (mode [local|remote])
 */
static int cmd_mode(uint8_t argc, char **argv)
{
    if (argc < 2)
    {
        shell_printf("Current mode: %s, sub=%d\r\n",
                     display_mgr_is_remote() ? "remote" : "local",
                     display_mgr_get_sub_mode());
        return 0;
    }

    if (strcmp(argv[1], "local") == 0)
    {
        display_mgr_set_remote(false);
        shell_printf("Switched to local mode\r\n");
    }
    else if (strcmp(argv[1], "remote") == 0)
    {
        display_mgr_set_remote(true);
        shell_printf("Switched to remote mode\r\n");
    }
    else
    {
        shell_printf("Usage: mode [local|remote]\r\n");
        return -1;
    }

    return 0;
}

/* ========================================================
 *  命令表定义
 * ======================================================== */

struct cmd cmd_table[] =
{
    { .name = "help",   .func = cmd_help,   .desc = "显示所有命令" },
    { .name = "clear",  .func = cmd_clear,  .desc = "清屏" },
    { .name = "info",   .func = cmd_info,   .desc = "系统信息" },
    { .name = "led",    .func = cmd_led,    .desc = "LED 控制 (led 0|1|2)" },
    { .name = "rd",     .func = cmd_rd,     .desc = "读内存 (rd <addr> [size])" },
    { .name = "reboot", .func = cmd_reboot, .desc = "软件复位" },
    { .name = "mode",   .func = cmd_mode,   .desc = "显示模式 (mode [local|remote])" },
    { .name = "update", .func = cmd_update, .desc = "进入固件更新模式" },
};

const uint16_t cmd_table_size = sizeof(cmd_table) / sizeof(cmd_table[0]);

/* 自动补全词表（用于参数补全） */
char *auto_complete_words[] =
{
    "0",
    "1",
    "2",
    "local",
    "remote",
    "-h",
};

const uint16_t auto_complete_words_size =
    sizeof(auto_complete_words) / sizeof(auto_complete_words[0]);

/**
 * @brief  初始化 CLI 命令（当前无需额外初始化，保留接口供扩展）
 */
void cli_cmds_init(void)
{
    /* 命令表已静态定义，暂无运行时注册需求 */
}



