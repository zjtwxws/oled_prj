/**
 * @file    boot_flash.h
 * @brief   Bootloader Flash 操作封装 — 扇区擦除、按字写入、CRC32
 */

#ifndef __BOOT_FLASH_H
#define __BOOT_FLASH_H

#include <stdint.h>

/* Slot A 扇区: S2~S6 */
#define SLOT_A_SECTORS    { FLASH_SECTOR_2, FLASH_SECTOR_3, FLASH_SECTOR_4, \
                            FLASH_SECTOR_5, FLASH_SECTOR_6 }
#define SLOT_A_SECTOR_COUNT  5

/* Slot B 扇区: S7~S9 */
#define SLOT_B_SECTORS    { FLASH_SECTOR_7, FLASH_SECTOR_8, FLASH_SECTOR_9 }
#define SLOT_B_SECTOR_COUNT  3

/**
 * @brief  解锁 Flash → 擦除指定扇区 → 锁定 Flash
 * @param  sector 扇区号 (FLASH_SECTOR_x)
 * @return 0=成功, 非0=失败
 */
int boot_erase_sector(uint32_t sector);

/**
 * @brief  擦除多个扇区 (依次擦除)
 * @param  sectors     扇区号数组
 * @param  count       扇区数量
 * @return 0=成功, 非0=失败
 */
int boot_erase_sectors(const uint32_t *sectors, uint8_t count);

/**
 * @brief  按字(4B)写入 Flash
 * @param  addr  目标地址 (必须 4 字节对齐)
 * @param  data  32位数据
 * @return 0=成功, 非0=失败
 */
int boot_flash_write_word(uint32_t addr, uint32_t data);

/**
 * @brief  擦除目标槽全部扇区
 * @param  slot  0=Slot A, 1=Slot B
 * @return 0=成功, 非0=失败
 */
int boot_erase_slot(uint8_t slot);

/**
 * @brief  从 Flash 地址读取 4 字节字 (大容量 Flash 可直接寻址)
 */
static inline uint32_t boot_flash_read_word(uint32_t addr)
{
    return *((volatile uint32_t *)addr);
}

/**
 * @brief  CRC32 (IEEE 802.3, 多项式 0xEDB88320)
 * @param  data  数据指针
 * @param  len   数据长度 (字节)
 * @return CRC32 校验值
 */
uint32_t boot_crc32(const uint8_t *data, uint32_t len);

/**
 * @brief  对整槽做 CRC32 校验
 * @param  slot      0=Slot A, 1=Slot B
 * @param  size      固件大小 (字节)
 * @return CRC32 值
 */
uint32_t boot_crc32_slot(uint8_t slot, uint32_t size);

#endif /* __BOOT_FLASH_H */
