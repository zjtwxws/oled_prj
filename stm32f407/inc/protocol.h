/**
 * @file    protocol.h
 * @brief   自定义二进制帧协议引擎
 */

#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#include <stdint.h>

#define PROTO_SOF       0xA5
#define PROTO_EOF       0x5A
#define PROTO_MAX_DATA  251
#define PROTO_FRAME_MAX (PROTO_MAX_DATA + 6)

/* 帧间超时恢复 (ms): 超过此时间未收到完整帧, 自动重置接收状态机 */
#define PROTO_RX_TIMEOUT_MS  500

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
#define CMD_FRAME_SYNC      0x20
#define CMD_ACK             0xF0
#define CMD_NAK             0xFF

#define NAK_CRC_ERROR       0x01
#define NAK_UNKNOWN_CMD     0x02
#define NAK_PARAM_ERROR     0x03
#define NAK_FLASH_ERROR     0x04
#define NAK_BUSY            0x05

typedef struct {
    uint8_t cmd;
    uint8_t seq;
    uint8_t len;
    uint8_t data[PROTO_MAX_DATA];
} proto_frame_t;

int proto_feed_byte(uint8_t byte);
const proto_frame_t* proto_get_frame(void);
void proto_reset_rx(void);

/*
 * 超时检查: 在调用 proto_feed_byte 的主循环中,
 * 若超过 PROTO_RX_TIMEOUT_MS 未收到完整帧, 调用此函数复位状态机。
 * 用法:
 *   if (HAL_GetTick() - last_byte_tick > PROTO_RX_TIMEOUT_MS) {
 *       proto_reset_rx();
 *       last_byte_tick = HAL_GetTick();
 *   }
 */
uint32_t proto_get_last_byte_tick(void);

uint16_t proto_build_frame(uint8_t cmd, uint8_t seq, const uint8_t *data, uint8_t len);
const uint8_t* proto_get_tx_buf(void);
uint8_t proto_crc8(const uint8_t *data, uint16_t len);

#endif
