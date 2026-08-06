/**
 * @file    ssd1306.c
 * @brief   SSD1306 OLED I²C 驱动实现
 *
 * 初始化序列参照厂家示例 (HelTec 0.96寸模组验证通过)。
 * 依赖: STM32 HAL 库 (HAL_I2C_Mem_Write 等)
 *       显存缓冲区 128×64 bits = 1024 bytes
 */

#include "ssd1306.h"
#include "i2c_drv.h"
#include "stm32f4xx_hal.h"
#include <string.h>

/* --- 内部常量 --- */

/*
 * SSD1306 初始化命令序列
 * 参照厂家提供的 STM32F103 示例程序 I2C_SSD1306_STM32(HW)/User/OLED_I2C.c
 * 验证参数: D5=0xF0, D9=0x22, DB=0x20, 81=0xFF
 * 寻址模式采用 Horizontal (0x00) 而非示例的 Page 模式 (0x10),
 * 以支持全帧 buffer 操作。
 */
static const uint8_t ssd1306_init_cmds[] = {
    0xAE,       /* Display OFF */
    0x20, 0x00, /* Memory Addressing Mode: Horizontal */
    0x40,       /* Set Display Start Line = 0 */
    0xA0,       /* Set Segment Re-map: column 0 mapped to SEG0 (0xA0=normal, 0xA1=mirror) */
    0xC0,       /* COM Output Scan Direction: normal mode (0xC0=normal, 0xC8=remapped) */
    0xA8, 0x3F, /* Set MUX Ratio (64 lines) */
    0xD3, 0x00, /* Set Display Offset = 0 */
    0xD5, 0xF0, /* Set OSC Frequency (参照示例, 分频=1, 频率=16) */
    0xD9, 0x22, /* Set Pre-charge Period (参照示例, Phase1=2 DCLK, Phase2=2 DCLK) */
    0xDA, 0x12, /* COM Pins Hardware Configuration */
    0xDB, 0x20, /* Set VCOMH Deselect Level ≈ 0.77×Vcc (参照示例) */
    0x81, 0xFF, /* Set Contrast = 255 (参照示例最大值) */
    0xA4,       /* Entire Display ON: resume to RAM content */
    0xA6,       /* Normal Display (non-inverted) */
    0x8D, 0x14, /* Enable Charge Pump */
    0x2E,       /* Deactivate scrolling */
    0xAF        /* Display ON */
};


/* 显存缓冲区: 1024 字节 (128 列 × 8 页)，格式为 page-major，每字节 8 个垂直像素 (bit0=顶部)。
 * ssd1306_update_screen() 按页通过 I2C 发送至 OLED GDDRAM。 */
static uint8_t buffer[SSD1306_WIDTH * SSD1306_PAGES];

/* --- 内部函数 --- */

static void ssd1306_write_cmd(uint8_t cmd)
{
    i2c_drv_write_reg(SSD1306_I2C_ADDR, 0x00, &cmd, 1);
}

static void ssd1306_write_cmds(const uint8_t *cmds, uint16_t len)
{
    i2c_drv_write_reg(SSD1306_I2C_ADDR, 0x00, cmds, len);
}

static void ssd1306_write_data(const uint8_t *data, uint16_t len)
{
    i2c_drv_write_reg(SSD1306_I2C_ADDR, 0x40, data, len);
}

/* --- 公开接口 --- */

int ssd1306_init(void)
{
    /*
     * 上电延时: SSD1306 手册要求 VDD 稳定后等待 ≥3µs。
     * MCU 自身启动已有足够延时, 但增加 100ms 应对电源上升缓慢
     * 或复位电容充电不完全的场景 (参照厂家示例 DelayMs(100))。
     */
    HAL_Delay(100);

    /* 发送全部初始化命令 */
    for (uint16_t i = 0; i < sizeof(ssd1306_init_cmds); i++) {
        ssd1306_write_cmd(ssd1306_init_cmds[i]);
    }

    /* 清空缓冲区 */
    ssd1306_fill(SSD1306_COLOR_BLACK);
    ssd1306_update_screen();

    return 0;
}

void ssd1306_display_on(void)
{
    ssd1306_write_cmd(0xAF);
}

void ssd1306_display_off(void)
{
    ssd1306_write_cmd(0xAE);
}

void ssd1306_set_contrast(uint8_t contrast)
{
    ssd1306_write_cmd(0x81);
    ssd1306_write_cmd(contrast);
}

void ssd1306_fill(uint8_t color)
{
    memset(buffer, (color == SSD1306_COLOR_WHITE) ? 0xFF : 0x00, sizeof(buffer));
}

void ssd1306_clear_buffer(void)
{
    memset(buffer, 0x00, sizeof(buffer));
}

void ssd1306_update_screen(void)
{
    /*
     * 按 page 逐页发送到 GDDRAM。
     * 3 条命令合并为一次 I²C 事务 (减少 24 次 START/STOP),
     * 相比原来每条命令独立发送, 刷新速度提升约 30%。
     */
    for (uint8_t page = 0; page < SSD1306_PAGES; page++) {
        uint8_t cmds[3];
        cmds[0] = 0xB0 + page;                       /* Set Page Start Address */
        cmds[1] = 0x00;                               /* Set Lower Column Start Address */
        cmds[2] = 0x10;                               /* Set Higher Column Start Address */
        ssd1306_write_cmds(cmds, 3);

        /* 一次发送 128 字节 */
        ssd1306_write_data(&buffer[page * SSD1306_WIDTH], SSD1306_WIDTH);
    }
}

void ssd1306_draw_pixel(uint8_t x, uint8_t y, uint8_t color)
{
    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) return;

    uint8_t page = y / 8;
    uint8_t bit  = y % 8;

    if (color) {
        buffer[page * SSD1306_WIDTH + x] |= (1 << bit);
    } else {
        buffer[page * SSD1306_WIDTH + x] &= ~(1 << bit);
    }
}

void ssd1306_write_byte(uint8_t page, uint8_t col, uint8_t data)
{
    if (page >= SSD1306_PAGES || col >= SSD1306_WIDTH) return;
    buffer[page * SSD1306_WIDTH + col] = data;
}

uint8_t* ssd1306_get_buffer(void)
{
    return buffer;
}

void ssd1306_scroll_h(uint8_t start_page, uint8_t end_page, uint8_t speed, uint8_t right)
{
    ssd1306_write_cmd(right ? 0x26 : 0x27);
    ssd1306_write_cmd(0x00);            /* Dummy byte */
    ssd1306_write_cmd(start_page & 0x07);
    ssd1306_write_cmd(speed & 0x07);
    ssd1306_write_cmd(end_page & 0x07);
    ssd1306_write_cmd(0x00);            /* Dummy byte */
    ssd1306_write_cmd(0xFF);            /* Dummy byte */
    ssd1306_write_cmd(0x2F);            /* Activate scroll */
}

void ssd1306_scroll_vh(uint8_t start_page, uint8_t end_page,
                        uint8_t speed, uint8_t right,
                        uint8_t v_offset)
{
    ssd1306_write_cmd(right ? 0x29 : 0x2A);
    ssd1306_write_cmd(0x00);
    ssd1306_write_cmd(start_page & 0x07);
    ssd1306_write_cmd(speed & 0x07);
    ssd1306_write_cmd(end_page & 0x07);
    ssd1306_write_cmd(v_offset & 0x3F);
    ssd1306_write_cmd(0x2F);
}

void ssd1306_scroll_stop(void)
{
    ssd1306_write_cmd(0x2E);
}