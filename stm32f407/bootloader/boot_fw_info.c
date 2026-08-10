/**
 * @file    boot_fw_info.c
 * @brief   固件信息区 (S10) 管理实现
 */

#include "boot_fw_info.h"
#include "boot_flash.h"
#include <string.h>

static fw_info_t g_fw_info;

/**
 * @brief  加载 fw_info，无效时初始化默认值
 * @return 0=加载成功, 1=首次初始化
 */
int fw_info_load(void)
{
    const fw_info_t *flash_info = (const fw_info_t *)FW_INFO_ADDR;

    if (flash_info->magic == FW_INFO_MAGIC)
    {
        uint32_t calc_crc = boot_crc32((const uint8_t *)flash_info,
                                        sizeof(fw_info_t) - 4);
        if (calc_crc == flash_info->crc32)
        {
            memcpy(&g_fw_info, flash_info, sizeof(fw_info_t));
            return 0;
        }
    }

    /* 首次使用: 初始化默认 fw_info + 写入 S10 */
    memset(&g_fw_info, 0xFF, sizeof(fw_info_t));
    g_fw_info.magic = FW_INFO_MAGIC;
    g_fw_info.active_slot = 0;
    g_fw_info.slot_a_state = SLOT_STATE_INVALID;
    g_fw_info.slot_b_state = SLOT_STATE_INVALID;
    g_fw_info.ota_request = 0;
    g_fw_info.slot_a_size = 0;
    g_fw_info.slot_a_crc = 0;
    g_fw_info.slot_a_version = 0;
    g_fw_info.slot_b_size = 0;
    g_fw_info.slot_b_crc = 0;
    g_fw_info.slot_b_version = 0;

    boot_erase_sector(FW_INFO_SECTOR);

    uint32_t *src = (uint32_t *)&g_fw_info;
    uint32_t addr = FW_INFO_ADDR;
    for (uint32_t i = 0; i < sizeof(fw_info_t) / 4; i++)
    {
        boot_flash_write_word(addr, src[i]);
        addr += 4;
        src++;
    }

    return 1;
}

/**
 * @brief  获取 RAM 中的 fw_info
 */
const fw_info_t* fw_info_get(void)
{
    return &g_fw_info;
}

/**
 * @brief  保存 fw_info 到 S10
 */
int fw_info_save(void)
{
    /* 计算 CRC32 (不含 crc32 字段自身) */
    g_fw_info.crc32 = boot_crc32((const uint8_t *)&g_fw_info,
                                  sizeof(fw_info_t) - 4);

    boot_erase_sector(FW_INFO_SECTOR);

    uint32_t *src = (uint32_t *)&g_fw_info;
    uint32_t addr = FW_INFO_ADDR;
    for (uint32_t i = 0; i < sizeof(fw_info_t) / 4; i++)
    {
        boot_flash_write_word(addr, src[i]);
        addr += 4;
        src++;
    }

    return 0;
}

/**
 * @brief  设置活跃槽
 */
void fw_info_set_active_slot(uint8_t slot)
{
    if (g_fw_info.active_slot != slot)
    {
        g_fw_info.active_slot = slot;
        fw_info_save();
    }
}

/**
 * @brief  标记槽状态
 */
void fw_info_set_slot_state(uint8_t slot, uint8_t state)
{
    if (slot == 0)
    {
        g_fw_info.slot_a_state = state;
    }
    else
    {
        g_fw_info.slot_b_state = state;
    }
    fw_info_save();
}

/**
 * @brief  更新槽的固件信息
 */
void fw_info_set_slot_info(uint8_t slot, uint32_t size, uint32_t crc, uint32_t version)
{
    if (slot == 0)
    {
        g_fw_info.slot_a_size = size;
        g_fw_info.slot_a_crc = crc;
        g_fw_info.slot_a_version = version;
    }
    else
    {
        g_fw_info.slot_b_size = size;
        g_fw_info.slot_b_crc = crc;
        g_fw_info.slot_b_version = version;
    }
    fw_info_save();
}

/**
 * @brief  清除 ota_request
 */
void fw_info_clear_ota_request(void)
{
    g_fw_info.ota_request = 0;
    fw_info_save();
}

/**
 * @brief  APP 侧设置 ota_request (只写单字节)
 * @note   由 user_app.c 在收到 OTA 命令时调用
 */
int fw_info_set_ota_request(void)
{
    uint32_t flash_addr = FW_INFO_ADDR
                          + ((uint32_t)&((fw_info_t *)0)->ota_request);

    /* 检查目标字节是否为 0xFF (已擦除态)，可直接编程 */
    if (*((volatile uint8_t *)flash_addr) == 0x01)
    {
        return 0; /* 已设置，无需重复写 */
    }

    /* 写入 ota_request = 1 (利用 Flash 字节编程: 写入到同一字内预读 3 字节) */
    uint32_t word_addr = flash_addr & ~3UL;
    uint8_t  byte_off  = flash_addr & 3;
    uint32_t word_val  = *((volatile uint32_t *)word_addr);

    /* 将目标字节替换为 0x01 */
    ((uint8_t *)&word_val)[byte_off] = 0x01;

    return boot_flash_write_word(word_addr, word_val);
}
