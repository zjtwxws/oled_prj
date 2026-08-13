/**
 * @file    boot_fw_info.c
 * @brief   fw info management
 */
#include "boot_fw_info.h"
#include "boot_flash.h"
#include "boot_debug.h"
#include <string.h>

/** @brief FW 模块调试日志 — 复用 boot_main.c 的 boot_printf，保持 [FW] 前缀 */
#define FW_DBG(fmt, ...) \
    do { \
        boot_printf("[FW] %s:%d " fmt "\r\n", __func__, __LINE__, ##__VA_ARGS__); \
    } while (0)

#define FW_INFO_SLOT_COUNT   (FW_INFO_SECTOR_SIZE / sizeof(fw_info_t))

static fw_info_t g_fw_info;

static const fw_info_t* fw_info_scan_last(uint32_t *out_index)
{
    const fw_info_t *base = (const fw_info_t *)FW_INFO_ADDR;
    const fw_info_t *last = NULL;
    uint32_t last_idx = 0;

    for (uint32_t i = 0; i < FW_INFO_SLOT_COUNT; i++)
    {
        if (base[i].magic != FW_INFO_MAGIC)
        {
            continue;
        }
        uint32_t calc_crc = boot_crc32((const uint8_t *)&base[i],
                                        sizeof(fw_info_t) - 4);
        if (calc_crc == base[i].crc32)
        {
            last = &base[i];
            last_idx = i;
        }
    }

    if (out_index != NULL)
    {
        *out_index = last_idx;
    }
    return last;
}

int fw_info_load(void)
{
    uint32_t last_idx;
    const fw_info_t *flash_info = fw_info_scan_last(&last_idx);

    FW_DBG("fw_info_load: scanning S10...");
    if (flash_info != NULL)
    {
        FW_DBG("fw_info_load: found valid record at index %u", last_idx);
        memcpy(&g_fw_info, flash_info, sizeof(fw_info_t));
        FW_DBG("fw_info_load: loaded OK");
        return 0;
    }

    FW_DBG("fw_info_load: no valid record, first boot init");

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

    g_fw_info.crc32 = boot_crc32((const uint8_t *)&g_fw_info,
                                  sizeof(fw_info_t) - 4);

    FW_DBG("fw_info_load: erasing S10 before first record...");
    if (boot_erase_sector(FW_INFO_SECTOR) != 0)
    {
        FW_DBG("fw_info_load: erase S10 FAILED");
        return 1;
    }

    FW_DBG("fw_info_load: writing first record to slot 0...");

    uint32_t *src = (uint32_t *)&g_fw_info;
    uint32_t addr = FW_INFO_ADDR;
    for (uint32_t i = 0; i < sizeof(fw_info_t) / 4; i++)
    {
        if (boot_flash_write_word_checked(addr, *src) != 0)
        {
            FW_DBG("fw_info_load: write FAILED");
            return 1;
        }
        addr += 4;
        src++;
    }

    FW_DBG("fw_info_load: first boot write OK");
    return 1;
}

const fw_info_t* fw_info_get(void)
{
    return &g_fw_info;
}

int fw_info_save(void)
{
    FW_DBG("fw_info_save: erasing S10...");
    if (boot_erase_sector(FW_INFO_SECTOR) != 0)
    {
        FW_DBG("fw_info_save: erase S10 FAILED");
        return -1;
    }

    g_fw_info.crc32 = boot_crc32((const uint8_t *)&g_fw_info,
                                  sizeof(fw_info_t) - 4);

    uint32_t *src = (uint32_t *)&g_fw_info;
    uint32_t addr = FW_INFO_ADDR;
    for (uint32_t i = 0; i < sizeof(fw_info_t) / 4; i++)
    {
        if (boot_flash_write_word_checked(addr, *src) != 0)
        {
            FW_DBG("fw_info_save: write/verify FAILED at 0x%08X", addr);
            return -1;
        }
        addr += 4;
        src++;
    }

    FW_DBG("fw_info_save: written and verified to S10 slot 0");
    return 0;
}

void fw_info_set_active_slot(uint8_t slot)
{
    if (g_fw_info.active_slot != slot)
    {
        g_fw_info.active_slot = slot;
        fw_info_save();
    }
}

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
 * @brief  激活槽位 — 一次性设置槽信息、状态和活跃槽，仅触发一次 flash 保存
 * @param  slot    槽号 (0=A, 1=B)
 * @param  size    固件大小
 * @param  crc     固件 CRC32
 * @param  version 固件版本号
 * @date   2026-08-12
 */
void fw_info_activate_slot(uint8_t slot, uint32_t size, uint32_t crc, uint32_t version)
{
    g_fw_info.magic = FW_INFO_MAGIC;

    if (slot == 0)
    {
        g_fw_info.slot_a_size = size;
        g_fw_info.slot_a_crc = crc;
        g_fw_info.slot_a_version = version;
        g_fw_info.slot_a_state = SLOT_STATE_VALID;
    }
    else
    {
        g_fw_info.slot_b_size = size;
        g_fw_info.slot_b_crc = crc;
        g_fw_info.slot_b_version = version;
        g_fw_info.slot_b_state = SLOT_STATE_VALID;
    }
    g_fw_info.active_slot = slot;
    fw_info_save();
}

void fw_info_clear_ota_request(void)
{
    g_fw_info.ota_request = 0;
    fw_info_save();
}

int fw_info_set_ota_request(void)
{
    uint32_t flash_addr = FW_INFO_ADDR
                          + ((uint32_t)&((fw_info_t *)0)->ota_request);

    if (*((volatile uint8_t *)flash_addr) == 0x01)
    {
        return 0;
    }

    uint32_t word_addr = flash_addr & ~3UL;
    uint8_t  byte_off  = flash_addr & 3;
    uint32_t word_val  = *((volatile uint32_t *)word_addr);

    ((uint8_t *)&word_val)[byte_off] = 0x01;

    return boot_flash_write_word(word_addr, word_val);
}
/* 生产版本注释此行 */
