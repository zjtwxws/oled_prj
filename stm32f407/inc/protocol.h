/**
 * @file    protocol.h
 * @brief   自定义二进制帧协议引擎
 */

#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#include <stdint.h>

#define PROTO_SOF       0xA5        /* 帧头 Start Of Frame */
#define PROTO_EOF       0x5A        /* 帧尾 End Of Frame */
#define PROTO_MAX_DATA  251      /* 负载数据最大长度 (字节) */
#define PROTO_FRAME_MAX (PROTO_MAX_DATA + 6)  /* 完整帧最大字节数 (SOF+LEN+CMD+SEQ+DATA+CRC+EOF = 头部4+数据+尾部2) */

/* 帧间超时恢复 (ms): 超过此时间未收到完整帧, 自动重置接收状态机 */
#define PROTO_RX_TIMEOUT_MS  500

#define CMD_LED_CTRL        0x01        /* PC->STM: LED 控制 (关/开/闪烁) */
#define CMD_DISPLAY_MODE    0x02    /* PC->STM: 显示模式切换 (本地/远程+子模式) */
#define CMD_TEXT_CONTENT    0x03    /* PC->STM: 下发文字内容 (UTF-8) */
#define CMD_TIME_SYNC       0x04       /* PC->STM: 时间同步 (年/月/日/时/分/秒/星期) */
#define CMD_WEATHER_DATA    0x05    /* PC->STM: 天气数据 (类型/温度/湿度/风向) */
#define CMD_BOOT_TEXT       0x06       /* PC->STM: 上电文字配置 (写入 Flash) */
#define CMD_OTA_RESERVED    0x07    /* 预留: OTA 升级 */
#define CMD_OTA_DATA        0x08    /* PC->STM: OTA 固件数据块 [offset:4B][payload:N] */
#define CMD_OTA_FINISH      0x09    /* PC->STM: OTA 传输完成, 校验激活 */
#define CMD_OTA_ABORT       0x0A    /* PC->STM: 取消 OTA 升级 */

#define CMD_LED_STATUS      0x10      /* STM->PC: LED 当前状态上报 */
#define CMD_MODE_STATUS     0x11     /* STM->PC: 当前显示模式状态上报 */
#define CMD_KEY_EVENT       0x12       /* STM->PC: 按键事件上报 */
#define CMD_FRAME_SYNC      0x20      /* PC->STM: 帧缓冲分段数据 (远程模式) */
#define CMD_ACK             0xF0             /* 确认应答 (ACK) */
#define CMD_NAK             0xFF             /* 否定应答 (NAK) */

#define NAK_CRC_ERROR       0x01       /* CRC 校验错误 */
#define NAK_UNKNOWN_CMD     0x02     /* 未知命令字 */
#define NAK_PARAM_ERROR     0x03     /* 参数错误 */
#define NAK_FLASH_ERROR     0x04     /* Flash 写入/擦除错误 */
#define NAK_BUSY            0x05            /* 设备忙, 暂不可处理 */
#define NAK_OTA_OFFSET      0x06     /* OTA 写入偏移越界 */
#define NAK_OTA_CRC         0x07     /* OTA 整体 CRC32 校验失败 */
#define NAK_OTA_ERASE       0x08     /* OTA Flash 擦除失败 */


typedef struct
{
    uint8_t cmd;    /* 命令字 (CMD_*) */
    uint8_t seq;    /* 序列号 (用于请求-应答匹配) */
    uint8_t len;    /* 负载数据长度 (字节) */
    uint8_t data[PROTO_MAX_DATA];  /* 负载数据缓冲区 */
} proto_frame_t;

/** @brief 向协议引擎送入一个字节，返回 1 表示收到完整帧 */
int proto_feed_byte(uint8_t byte);
/** @brief 获取最近一次解码成功的帧指针 */
const proto_frame_t* proto_get_frame(void);
/** @brief 复位接收状态机 (超时或错误恢复) */
void proto_reset_rx(void);

/*
 * 超时检查: 在调用 proto_feed_byte 的主循环中,
 * 若超过 PROTO_RX_TIMEOUT_MS 未收到完整帧, 调用此函数复位状态机。
 * 用法:
 *   if (HAL_GetTick() - last_byte_tick > PROTO_RX_TIMEOUT_MS)
 {
 *       proto_reset_rx();
 *       last_byte_tick = HAL_GetTick();
 *   }
 */
uint32_t proto_get_last_byte_tick(void);

/** @brief 构建发送帧 (SOF+LEN+CMD+SEQ+DATA+CRC+EOF)，返回帧总长度 */
uint16_t proto_build_frame(uint8_t cmd, uint8_t seq, const uint8_t *data, uint8_t len);
/** @brief 获取发送缓冲区指针 (每次 proto_build_frame 覆盖) */
const uint8_t* proto_get_tx_buf(void);
/** @brief 计算 CRC-8-ATM 校验值 (查表法) */
uint8_t proto_crc8(const uint8_t *data, uint16_t len);

#endif
