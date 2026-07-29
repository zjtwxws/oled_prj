/**
 * @file    protocol.h
 * @brief   RK3506 端 — 与 STM32 相同的二进制帧协议 (纯 C)
 *
 * Copyright (c) 2025
 */

#ifndef RK_PROTOCOL_H
#define RK_PROTOCOL_H

#include <stdint.h>
#include <string.h>

#define PROTO_SOF       0xA5
#define PROTO_EOF       0x5A
#define PROTO_MAX_DATA  251
#define PROTO_FRAME_MAX (PROTO_MAX_DATA + 6)

/* 命令码 (与 STM32 一致) */
typedef enum {
    CMD_LED_CTRL      = 0x01,
    CMD_DISPLAY_MODE  = 0x02,
    CMD_TEXT_CONTENT  = 0x03,
    CMD_TIME_SYNC     = 0x04,
    CMD_WEATHER_DATA  = 0x05,
    CMD_BOOT_TEXT     = 0x06,
    CMD_OTA_RESERVED  = 0x07,
    CMD_LED_STATUS    = 0x10,
    CMD_MODE_STATUS   = 0x11,
    CMD_KEY_EVENT     = 0x12,
    CMD_FRAME_SYNC    = 0x20,   /* STM32 → PC: OLED 显存同步帧 */
    CMD_ACK           = 0xF0,
    CMD_NAK           = 0xFF
} ProtoCmd;

/* 错误码 */
typedef enum {
    NAK_CRC_ERROR   = 0x01,
    NAK_UNKNOWN_CMD = 0x02,
    NAK_PARAM_ERROR = 0x03,
    NAK_FLASH_ERROR = 0x04,
    NAK_BUSY        = 0x05
} ProtoError;

/* 帧结构 */
typedef struct {
    uint8_t cmd;
    uint8_t seq;
    uint8_t len;
    uint8_t data[PROTO_MAX_DATA];
} ProtoFrame;

/* 协议解析器 (状态机) */
typedef struct {
    int       rx_state;      /* RxState enum */
    ProtoFrame rx_frame;
    uint8_t   rx_data_idx;
    uint8_t   rx_expected_len;
    uint8_t   rx_crc_accum;

    uint8_t   tx_buf[PROTO_FRAME_MAX];
    uint16_t  tx_len;
} ProtoParser;

/* 接收器状态机 */
enum {
    PROTO_WAIT_SOF  = 0,
    PROTO_WAIT_LEN  = 1,
    PROTO_WAIT_CMD  = 2,
    PROTO_WAIT_SEQ  = 3,
    PROTO_WAIT_DATA = 4,
    PROTO_WAIT_CRC  = 5,
    PROTO_WAIT_EOF  = 6
};

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 */
void proto_init(ProtoParser* p);

/* 接收器: 喂入一个字节, 返回 1 表示完整帧已就绪 */
int  proto_feed(ProtoParser* p, uint8_t byte);

/* 获取已接收的帧 */
const ProtoFrame* proto_get_frame(const ProtoParser* p);

/* 重置状态机 */
void proto_reset(ProtoParser* p);

/* 发送器: 构建一帧, 返回帧总长度 */
uint16_t proto_build(ProtoParser* p, uint8_t cmd, uint8_t seq,
                     const uint8_t* data, uint8_t len);

/* 获取发送缓冲区 */
const uint8_t* proto_tx_buf(const ProtoParser* p);

/* CRC8 计算 */
uint8_t proto_crc8(const uint8_t* data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* RK_PROTOCOL_H */
