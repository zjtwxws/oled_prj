/**
 * @file    protocol.h
 * @brief   自定义二进制帧协议引擎
 *
 * 帧格式: SOF(0xA5) | LEN | CMD | SEQ | DATA(0~251B) | CRC8 | EOF(0x5A)
 */

#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#include <stdint.h>

/* 帧边界 */
#define PROTO_SOF       0xA5
#define PROTO_EOF       0x5A
#define PROTO_MAX_DATA  251
#define PROTO_FRAME_MAX (PROTO_MAX_DATA + 6)  /* 257 */

/* 命令码 */
#define CMD_LED_CTRL        0x01
#define CMD_DISPLAY_MODE    0x02
#define CMD_TEXT_CONTENT    0x03
#define CMD_TIME_SYNC       0x04
#define CMD_WEATHER_DATA    0x05
#define CMD_BOOT_TEXT       0x06
#define CMD_OTA_RESERVED    0x07
#define CMD_LED_STATUS      0x10
#define CMD_MODE_STATUS     0x11
#define CMD_KEY_EVENT       0x12
#define CMD_ACK             0xF0
#define CMD_NAK             0xFF

/* 错误码 */
#define NAK_CRC_ERROR       0x01
#define NAK_UNKNOWN_CMD     0x02
#define NAK_PARAM_ERROR     0x03
#define NAK_FLASH_ERROR     0x04
#define NAK_BUSY            0x05

/* 帧结构 */
typedef struct {
    uint8_t cmd;
    uint8_t seq;
    uint8_t len;
    uint8_t data[PROTO_MAX_DATA];
} proto_frame_t;

/* --- 接收器 --- */

/**
 * @brief  喂入一个字节到协议解析器
 * @param  byte  接收到的字节
 * @return 1=解析完成一帧 (frame 有效), 0=继续接收
 * @note   解析完成的帧通过 proto_get_frame() 获取
 */
int proto_feed_byte(uint8_t byte);

/**
 * @brief  获取最近解析完成的帧
 */
const proto_frame_t* proto_get_frame(void);

/**
 * @brief  重置接收器状态
 */
void proto_reset_rx(void);

/* --- 发送器 --- */

/**
 * @brief  构建一帧数据到发送缓冲区
 * @param  cmd    命令码
 * @param  seq    序号
 * @param  data   数据
 * @param  len    数据长度
 * @return 构建后的帧总长度 (不包括终止符)
 */
uint16_t proto_build_frame(uint8_t cmd, uint8_t seq, const uint8_t *data, uint8_t len);

/**
 * @brief  获取发送缓冲区 (用于 UART 发送)
 */
const uint8_t* proto_get_tx_buf(void);

/**
 * @brief  计算 CRC-8-ATM
 */
uint8_t proto_crc8(const uint8_t *data, uint16_t len);

#endif /* __PROTOCOL_H */
