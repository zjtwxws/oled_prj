/**
 * @file    boot_oled.h
 * @brief   Bootloader OLED 显示 — SSD1306 位控 I2C 驱动 + 精简字库
 */

#ifndef __BOOT_OLED_H
#define __BOOT_OLED_H

#include <stdint.h>

#define OLED_WIDTH   128
#define OLED_HEIGHT  64
#define OLED_PAGES   (OLED_HEIGHT / 8)

void boot_oled_init(void);
void boot_oled_clear(void);
void boot_oled_flush(void);
void boot_oled_set_cursor(uint8_t page, uint8_t col);
void boot_oled_status(const char *text);
void boot_oled_progress(uint32_t done, uint32_t total);

#endif /* __BOOT_OLED_H */
