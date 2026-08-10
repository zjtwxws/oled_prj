/**
 * @file    boot_fw_info.h
 * @brief   固件信息区 (S10) 管理 — 槽状态、CRC、版本号
 */

#ifndef __BOOT_FW_INFO_H
#define __BOOT_FW_INFO_H

#include <stdint.h>

/* ---- 分区地址 ---- */
#define FW_INFO_ADDR        0x080C0000UL
/* S10 扇区号 = 0x00000050U (与 HAL 的 FLASH_SECTOR_10 等值, 此处避免引入 HAL 头文件) */
#define FW_INFO_SECTOR      ((uint32_t)0x00000050U)
#define FW_INFO_MAGIC       0x4657494EUL  /* "FWIN" */

#define SLOT_A_ADDR         0x08008000UL
#define SLOT_A_SIZE         0x00058000UL  /* 352KB: S2~S6 */

#define SLOT_B_ADDR         0x08060000UL
#define SLOT_B_SIZE         0x00060000UL  /* 384KB: S7~S9 */

#define SLOT_STATE_INVALID  0xFF
#define SLOT_STATE_VALID    0x01

/* ---- 固件信息结构体 (64字节, 4字节对齐) ---- */
typedef struct
{
    uint32_t magic;             /* 魔数 0x4657494E "FWIN" */
    uint8_t  active_slot;       /* 0=Slot A, 1=Slot B */
    uint8_t  slot_a_state;      /* 0xFF=无效, 0x01=有效 */
    uint8_t  slot_b_state;
    uint8_t  ota_request;       /* 1=APP 请求进入 Bootloader */
    uint32_t slot_a_size;       /* A 槽固件大小 (字节) */
    uint32_t slot_a_crc;        /* A 槽固件 CRC32 */
    uint32_t slot_a_version;    /* A 槽版本号 */
    uint32_t slot_b_size;
    uint32_t slot_b_crc;
    uint32_t slot_b_version;
    uint32_t crc32;             /* 本结构体前 48 字节的 CRC32 */
} fw_info_t;

/* ---- API ---- */

/**
 * @brief  加载 fw_info，无效时初始化默认值并写入 S10
 * @return 0=加载成功, 1=首次初始化
 */
int fw_info_load(void);

/**
 * @brief  获取 RAM 中缓存的 fw_info 指针
 */
const fw_info_t* fw_info_get(void);

/**
 * @brief  将 RAM 中的 fw_info 写入 S10 (擦除整个扇区后重写)
 * @return 0=成功, 非0=失败
 */
int fw_info_save(void);

/**
 * @brief  设置活跃槽并保存
 */
void fw_info_set_active_slot(uint8_t slot);

/**
 * @brief  标记槽状态并保存
 */
void fw_info_set_slot_state(uint8_t slot, uint8_t state);

/**
 * @brief  更新槽的固件信息 (size + crc32 + version) 并保存
 */
void fw_info_set_slot_info(uint8_t slot, uint32_t size, uint32_t crc, uint32_t version);

/**
 * @brief  清除 ota_request 标志并保存
 */
void fw_info_clear_ota_request(void);

/**
 * @brief  APP 侧调用: 设置 ota_request 为 1 (只写单字节，不擦扇区)
 * @return 0=成功, 非0=失败
 */
int fw_info_set_ota_request(void);

#endif /* __BOOT_FW_INFO_H */
