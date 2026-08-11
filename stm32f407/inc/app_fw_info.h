/**
 * @file    app_fw_info.h
 * @brief   APP 侧固件信息访问接口 (只读 fw_info 结构 + 写 ota_request)
 */

#ifndef __APP_FW_INFO_H
#define __APP_FW_INFO_H

#include <stdint.h>

/* ---- 分区地址 ---- */
/** fw_info 在 S10 的扇区地址 */
#define APP_FW_INFO_ADDR    0x080C0000UL
/** S10 扇区大小 (128KB) */
#define FW_INFO_SECTOR_SIZE 0x00020000UL
/** fw_info 结构体魔数 "FWIN" */
#define FW_INFO_MAGIC       0x4657494EUL

/** fw_info_t 内部 ota_request 字段偏移量 */
#define APP_FW_INFO_OTA_REQ_OFFSET  7

/* Slot 基地址 */
#ifndef APP_VTOR_ADDR
#ifndef APP_SLOT_B
#define APP_VTOR_ADDR       0x08000000UL
#else
#define APP_VTOR_ADDR       0x08060000UL
#endif
#endif

/* ---- 固件信息结构体 (与 boot_fw_info.h 完全对齐) ---- */
typedef struct
{
    uint32_t magic;             /** 魔数 0x4657494E "FWIN" */
    uint8_t  active_slot;       /** 0=Slot A, 1=Slot B */
    uint8_t  slot_a_state;      /** 0xFF=无效, 0x01=有效 */
    uint8_t  slot_b_state;
    uint8_t  ota_request;       /** 1=APP 请求进入 Bootloader */
    uint32_t slot_a_size;       /** A 槽固件大小 (字节) */
    uint32_t slot_a_crc;        /** A 槽固件 CRC32 */
    uint32_t slot_a_version;    /** A 槽版本号 */
    uint32_t slot_b_size;
    uint32_t slot_b_crc;
    uint32_t slot_b_version;
    uint32_t crc32;             /** 本结构体前 32 字节的 CRC32 */
} fw_info_t;

/**
 * @brief  设置 ota_request = 1。扫描 S10 找到最后一条有效记录后写入正确的地址。
 * @return 0=成功, 非0=失败
 */
int app_fw_info_set_ota_request(void);

#endif /* __APP_FW_INFO_H */