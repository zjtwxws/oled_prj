/**
 * @file    boot_proto.h
 * @brief   Bootloader 简化协议引擎 — SOF+LEN+CMD+DATA+CRC+EOF 帧解析
 */

#ifndef __BOOT_PROTO_H
#define __BOOT_PROTO_H

#include <stdint.h>

/* 帧常量 */
#define PROTO_SOF          0xA5
#define PROTO_EOF          0x5A
#define PROTO_MAX_DATA     251
#define PROTO_FRAME_MAX    (PROTO_MAX_DATA + 6)

/* OTA 命令 (PC→STM32 Bootloader) */
#define CMD_OTA_START      0x07   /* [slot:1B][size:4B LE][crc32:4B LE][version:4B LE] */
#define CMD_OTA_DATA       0x08   /* [offset:4B LE][payload:N] */
#define CMD_OTA_FINISH     0x09   /* 无负载 */
#define CMD_OTA_ABORT      0x0A   /* 无负载 */

/* 应答命令 */
#define CMD_ACK            0xF0
#define CMD_NAK            0xFF

/* 错误码 */
#define NAK_CRC_ERROR      0x01
#define NAK_UNKNOWN_CMD    0x02
#define NAK_PARAM_ERROR    0x03
#define NAK_FLASH_ERROR    0x04
#define NAK_BUSY           0x05
#define NAK_OTA_OFFSET     0x06
#define NAK_OTA_CRC        0x07
#define NAK_OTA_ERASE      0x08

/* 解码后的帧 */
typedef struct
{
    uint8_t cmd;
    uint8_t len;
    uint8_t data[PROTO_MAX_DATA];
} boot_frame_t;

/**
 * @brief  逐字节喂入协议解析器
 * @param  byte  接收到的字节
 * @return 1=完整帧已解码 (通过 boot_proto_get_frame 获取)
 */
int boot_proto_feed(uint8_t byte);

/**
 * @brief  获取最近解码成功的帧指针
 */
const boot_frame_t* boot_proto_get_frame(void);

/**
 * @brief  复位接收状态机
 */
void boot_proto_reset(void);

/**
 * @brief  构建发送帧 (SOF+LEN+CMD+DATA+CRC+EOF)
 * @param  cmd    命令码
 * @param  data   负载数据
 * @param  len    数据长度
 * @return 帧总字节数
 */
uint16_t boot_proto_build(uint8_t cmd, const uint8_t *data, uint8_t len);

/**
 * @brief  获取发送缓冲区指针
 */
const uint8_t* boot_proto_tx_buf(void);

#endif /* __BOOT_PROTO_H */
