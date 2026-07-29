/**
 * @file    font.h
 * @brief   字库管理模块
 *
 * 支持 ASCII 8×16 和中文 GB2312 16×16 点阵取模。
 * 字模数据存放在 font_data.c 中 (编译时链接, 存于 Flash)。
 */

#ifndef __FONT_H
#define __FONT_H

#include <stdint.h>

/* 字体尺寸 */
#define FONT_ASCII_W    8
#define FONT_ASCII_H    16

#define FONT_CHINESE_W  16
#define FONT_CHINESE_H  16

/**
 * @brief  获取 ASCII 字符字模 (8×16)
 * @param  ch   ASCII 字符 (0x20~0x7E)
 * @return 指向 16 字节字模数据的指针, 每字节代表一列
 */
const uint8_t* font_get_ascii(char ch);

/**
 * @brief  获取中文字符字模 (16×16)
 * @param  gb2312_code  GB2312 内码 (两字节拼成的 uint16_t, 高字节在前)
 * @return 指向 32 字节字模数据的指针, 或 NULL 表示不在字库中
 * @note   字库尚不完整时返回 NULL, 调用方需处理
 */
const uint8_t* font_get_chinese(uint16_t gb2312_code);

/**
 * @brief  判断是否为中文字符 (判断首字节范围)
 * @param  byte  待判断字节
 * @return 1=是 GB2312 首字节 (0xA1~0xF7), 0=否
 */
int font_is_chinese_lead(uint8_t byte);

/**
 * @brief  简化版: 直接通过 UTF-8 序列获取汉字字模
 * @param  utf8_str  指向 UTF-8 编码的中文字符 (3 字节)
 * @return 字模指针或 NULL
 */
const uint8_t* font_get_chinese_utf8(const char *utf8_str);

#endif /* __FONT_H */
