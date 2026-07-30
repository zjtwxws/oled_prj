/**
 * @file    ssd1306.h
 * @brief   SSD1306 OLED 底层驱动接口 (I²C)
 */

#ifndef __SSD1306_H
#define __SSD1306_H

#include <stdint.h>

/* OLED 分辨率 */
#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64
#define SSD1306_PAGES   (SSD1306_HEIGHT / 8)  /* 8 pages */

/*
 * I²C 从机地址
 * 7-bit SA0=0 → 0x3C, SA0=1 → 0x3D
 * 模组默认 SA0=0 (通过板上电阻配置), 对应 I²C 总线地址 0x78 (左移 1 位后)
 * 若模组丝印为 0x7A, 则 SA0=1, 需改为 0x3D
 */
#define SSD1306_I2C_ADDR    0x3C

/* I²C 操作超时 (ms), 避免 I²C 总线异常时永久阻塞主循环 */
#define SSD1306_I2C_TIMEOUT_MS  100

/* 基本颜色 */
#define SSD1306_COLOR_BLACK  0
#define SSD1306_COLOR_WHITE  1

/* 显示状态 */
typedef enum {
    SSD1306_STATE_OFF = 0,
    SSD1306_STATE_ON  = 1
} ssd1306_state_t;

/* --- 公开接口 --- */

/**
 * @brief  初始化 SSD1306 (发送初始化命令序列, 含 100ms 上电延时)
 * @return 0=成功, 非0=失败
 */
int ssd1306_init(void);

/**
 * @brief  设置显示开关
 */
void ssd1306_display_on(void);
void ssd1306_display_off(void);

/**
 * @brief  设置对比度 (0~255)
 */
void ssd1306_set_contrast(uint8_t contrast);

/**
 * @brief  全屏填充
 * @param color  SSD1306_COLOR_BLACK / SSD1306_COLOR_WHITE
 */
void ssd1306_fill(uint8_t color);

/**
 * @brief  更新显存到 OLED (全屏刷新)
 * @note   将内部 buffer 通过 I²C 发送至 OLED GDDRAM
 */
void ssd1306_update_screen(void);

/**
 * @brief  清除显存缓冲区 (不刷新 OLED)
 */
void ssd1306_clear_buffer(void);

/**
 * @brief  在指定位置绘制一个像素 (写入 buffer)
 */
void ssd1306_draw_pixel(uint8_t x, uint8_t y, uint8_t color);

/**
 * @brief  在指定 page 写入一字节 (8 个垂直像素)
 * @param page  页号 (0~7)
 * @param col   列号 (0~127)
 * @param data  8bit 像素数据 (bit0=top)
 */
void ssd1306_write_byte(uint8_t page, uint8_t col, uint8_t data);

/**
 * @brief  获取显存缓冲区指针 (用于特效算法直接操作)
 */
uint8_t* ssd1306_get_buffer(void);

/**
 * @brief  设置水平滚动
 * @param start_page  起始页 (0~7)
 * @param end_page    结束页 (0~7)
 * @param speed       滚动速度 0~7 (0最快)
 * @param right       1=向右, 0=向左
 */
void ssd1306_scroll_h(uint8_t start_page, uint8_t end_page, uint8_t speed, uint8_t right);

/**
 * @brief  设置垂直+水平滚动
 */
void ssd1306_scroll_vh(uint8_t start_page, uint8_t end_page,
                        uint8_t speed, uint8_t right,
                        uint8_t v_offset);

/**
 * @brief  停止滚动
 */
void ssd1306_scroll_stop(void);

#endif /* __SSD1306_H */
