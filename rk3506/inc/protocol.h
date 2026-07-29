/**
 * @file    protocol.h
 * @brief   RK3506 端 — 与 STM32 相同的二进制帧协议 (C++ 兼容)
 */

#ifndef RK_PROTOCOL_H
#define RK_PROTOCOL_H

#include <cstdint>
#include <cstring>

#define PROTO_SOF       0xA5
#define PROTO_EOF       0x5A
#define PROTO_MAX_DATA  251
#define PROTO_FRAME_MAX (PROTO_MAX_DATA + 6)

/* 命令码 (与 STM32 一致) */
enum ProtoCmd : uint8_t {
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
    CMD_ACK           = 0xF0,
    CMD_NAK           = 0xFF
};

/* 错误码 */
enum ProtoError : uint8_t {
    NAK_CRC_ERROR   = 0x01,
    NAK_UNKNOWN_CMD = 0x02,
    NAK_PARAM_ERROR = 0x03,
    NAK_FLASH_ERROR = 0x04,
    NAK_BUSY        = 0x05
};

/* 帧结构 */
struct ProtoFrame {
    uint8_t cmd;
    uint8_t seq;
    uint8_t len;
    uint8_t data[PROTO_MAX_DATA];
};

class Protocol {
public:
    Protocol();

    /* 接收器 (状态机) */
    int  feed(uint8_t byte);            /* 返回 1=完整帧 */
    const ProtoFrame* getFrame() const;
    void reset();

    /* 发送器 */
    uint16_t build(uint8_t cmd, uint8_t seq, const uint8_t* data, uint8_t len);
    const uint8_t* txBuf() const;

    /* CRC */
    static uint8_t crc8(const uint8_t* data, uint16_t len);

private:
    enum RxState {
        WAIT_SOF, WAIT_LEN, WAIT_CMD, WAIT_SEQ,
        WAIT_DATA, WAIT_CRC, WAIT_EOF
    };

    RxState    m_rxState;
    ProtoFrame m_rxFrame;
    uint8_t    m_rxDataIdx;
    uint8_t    m_rxExpectedLen;
    uint8_t    m_rxCrcAccum;

    uint8_t    m_txBuf[PROTO_FRAME_MAX];
    uint16_t   m_txLen;

    static const uint8_t crc8Table[256];
};

#endif /* RK_PROTOCOL_H */
