/**
 * @file    app_fw_info.c
 * @brief   APP 侧 fw_info 操作 — 写入 ota_request 标志
 *
 * 只写 S10 中 ota_request 单字节为 1。
 * 不擦除扇区 (S10 仅供 Bootloader 整体擦除管理)。
 */

#include "app_fw_info.h"
#include "stm32f4xx_hal.h"

/**
 * @brief  设置 ota_request = 1
 * @note   利用 Flash 字编程: 将 S10 偏移 13 处字节设为 0x01
 * @return 0=成功, 非0=失败
 */
int app_fw_info_set_ota_request(void)
{
    uint32_t addr = APP_FW_INFO_ADDR + APP_FW_INFO_OTA_REQ_OFFSET;

    /* 字节已在擦除态则跳过 */
    if (*((volatile uint8_t *)addr) == 0x01)
    {
        return 0;
    }

    uint32_t word_addr = addr & ~3UL;
    uint32_t word_val  = *((volatile uint32_t *)word_addr);
    uint8_t  byte_off  = addr & 3;

    ((uint8_t *)&word_val)[byte_off] = 0x01;

    HAL_FLASH_Unlock();
    HAL_StatusTypeDef ret = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                               word_addr, word_val);
    HAL_FLASH_Lock();

    return (ret == HAL_OK) ? 0 : -1;
}
