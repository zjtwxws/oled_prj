#pragma once

/**
 * @file    protocol.h
 * @brief   自定义二进制帧协议 — PC 上位机端
 *
 * 与 STM32F407 端 stm32f407/inc/protocol.h 完全对应。
 * CRC-8-ATM 查表与 STM32 端 crc8_table 一致。
 */

#include <cstdint>
#include <vector>

/* --- 帧常量 --- */
constexpr uint8_t  PROTO_SOF        = 0xA5;
constexpr uint8_t  PROTO_EOF        = 0x5A;
constexpr uint8_t  PROTO_MAX_DATA   = 251;
constexpr uint16_t PROTO_FRAME_MAX  = PROTO_MAX_DATA + 6;
constexpr uint32_t PROTO_RX_TIMEOUT_MS = 500;

/* --- 命令码 --- */
constexpr uint8_t CMD_LED_CTRL      = 0x01;
constexpr uint8_t CMD_DISPLAY_MODE  = 0x02;
constexpr uint8_t CMD_TEXT_CONTENT  = 0x03;
constexpr uint8_t CMD_TIME_SYNC     = 0x04;
constexpr uint8_t CMD_WEATHER_DATA  = 0x05;
constexpr uint8_t CMD_BOOT_TEXT     = 0x06;
constexpr uint8_t CMD_LED_STATUS    = 0x10;
constexpr uint8_t CMD_MODE_STATUS   = 0x11;
constexpr uint8_t CMD_KEY_EVENT     = 0x12;
constexpr uint8_t CMD_FRAME_SYNC    = 0x20;  /* v3.1: 远程模式帧缓冲分段下发 */
constexpr uint8_t CMD_ACK           = 0xF0;
constexpr uint8_t CMD_NAK           = 0xFF;

/* --- NAK 错误码 --- */
constexpr uint8_t NAK_CRC_ERROR     = 0x01;
constexpr uint8_t NAK_UNKNOWN_CMD   = 0x02;
constexpr uint8_t NAK_PARAM_ERROR   = 0x03;
constexpr uint8_t NAK_FLASH_ERROR   = 0x04;
constexpr uint8_t NAK_BUSY          = 0x05;

/* --- 显示模式 --- */
constexpr uint8_t DISP_MODE_STATIC   = 0;
constexpr uint8_t DISP_MODE_SCROLL_L = 1;
constexpr uint8_t DISP_MODE_SCROLL_R = 2;
constexpr uint8_t DISP_MODE_SCROLL_U = 3;
constexpr uint8_t DISP_MODE_SCROLL_D = 4;
constexpr uint8_t DISP_MODE_FLIP     = 5;
constexpr uint8_t DISP_MODE_FADE     = 6;
constexpr uint8_t DISP_MODE_COUNT    = 7;

/* --- 天气类型 --- */
constexpr uint8_t WEATHER_SUNNY  = 0;
constexpr uint8_t WEATHER_CLOUDY = 1;
constexpr uint8_t WEATHER_OVERCAST = 2;
constexpr uint8_t WEATHER_LIGHT_RAIN = 3;
constexpr uint8_t WEATHER_HEAVY_RAIN = 4;
constexpr uint8_t WEATHER_THUNDER = 5;
constexpr uint8_t WEATHER_SNOW   = 6;

/* --- 帧结构 --- */
struct ProtoFrame {
    uint8_t cmd;
    uint8_t seq;
    uint8_t len;
    uint8_t data[PROTO_MAX_DATA];
};

/**
 * @brief 二进制帧协议解析器
 *
 * 状态机与 STM32 端 proto_feed_byte() 逻辑完全一致。
 * 使用方式：
 *   ProtocolParser parser;
 *   while (有字节到达) {
 *       if (parser.FeedByte(b)) {
 *           const ProtoFrame& f = parser.GetFrame();
 *           ProcessFrame(f);
 *       }
 *       // 定时检查超时
 *       if (parser.CheckTimeout(GetTickCount64())) {
 *           // 状态机已复位
 *       }
 *   }
 */
class ProtocolParser {
public:
    ProtocolParser();

    // 喂入一个字节，返回 true 表示一帧解析完成
    bool FeedByte(uint8_t byte);

    // 获取最新解析完成的帧
    const ProtoFrame& GetFrame() const { return m_rxFrame; }

    // 复位接收状态机
    void ResetRx();

    // 超时检测: 超过 PROTO_RX_TIMEOUT_MS 未收到完整帧则自动复位
    // currentMs: 当前毫秒时间戳 (GetTickCount64)
    // 返回 true 表示已复位
    bool CheckTimeout(uint64_t currentMs);

    // 构建发送帧 (含 SOF/LEN/CMD/SEQ/DATA/CRC8/EOF)
    static std::vector<uint8_t> BuildFrame(uint8_t cmd, uint8_t seq,
                                           const uint8_t* data, uint8_t len);

    // CRC-8-ATM 校验 (查表法, 与 STM32 端 crc8_table 完全一致)
    static uint8_t CRC8(const uint8_t* data, size_t len);

private:
    enum RxState {
        WAIT_SOF = 0,
        WAIT_LEN,
        WAIT_CMD,
        WAIT_SEQ,
        WAIT_DATA,
        WAIT_CRC,
        WAIT_EOF
    };

    RxState      m_state;
    ProtoFrame   m_rxFrame;
    uint8_t      m_dataIdx;
    uint8_t      m_expectedLen;
    uint8_t      m_crcAccum;
    uint64_t     m_lastByteTick;

    void RecordTick();
};
