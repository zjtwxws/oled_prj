/**
 * @file    boot_fw_info.c
 * @brief   fw info management
 */
#include "boot_fw_info.h"
#include "boot_flash.h"
#include "usart.h"
#define FW_DEBUG_ENABLE
#include <string.h>
#include <stdarg.h>


static void fw_dbg_impl(const char *func, int line, const char *fmt, ...)
{
    char buf[128];
    int len = 0;
    const char *p;
    va_list args;
    va_start(args, fmt);
    p = "[FW] "; while (*p && len < 126) buf[len++] = *p++;
    while (*func && len < 126) buf[len++] = *func++;
    if (len < 126) buf[len++] = ':';
    {
        char ln[7]; int l = line, i = 6;
        do { ln[--i] = (char)('0' + (l % 10)); l /= 10; } while (l > 0);
        while (i < 6 && len < 126) buf[len++] = ln[i++];
    }
    if (len < 126) buf[len++] = ' ';
    {
        char tmp[80]; char *dst = tmp; char *end = tmp + 79;
        const char *s = fmt;
        while (*s && dst < end) {
            if (*s != '%') { *dst++ = *s++; continue; }
            s++;
            if (*s == 'u') {
                unsigned int val = va_arg(args, unsigned int);
                char rev[12]; int ri = 0;
                do { rev[ri++] = (char)('0' + (val % 10)); val /= 10; } while (val && ri < 11);
                if (ri == 0) rev[ri++] = '0';
                while (ri > 0 && dst < end) *dst++ = rev[--ri];
                s++;
            } else if (*s == 's') {
                const char *str = va_arg(args, const char *);
                while (str && *str && dst < end) *dst++ = *str++;
                s++;
            } else if (*s == 'd') {
                int val = va_arg(args, int);
                if (val < 0) { if (dst < end) *dst++ = '-'; val = -val; }
                char rev[12]; int ri = 0;
                do { rev[ri++] = (char)('0' + (val % 10)); val /= 10; } while (val && ri < 11);
                if (ri == 0) rev[ri++] = '0';
                while (ri > 0 && dst < end) *dst++ = rev[--ri];
                s++;
            } else if (*s == '%') { if (dst < end) *dst++ = '%'; s++; }
            else if (*s == 'x' || *s == 'X') {
                unsigned int val = va_arg(args, unsigned int);
                char hex[9];
                for (int k = 0; k < 8; k++) {
                    unsigned int d = (val >> (28 - k * 4)) & 0xF;
                    hex[k] = (char)(d < 10 ? '0' + d : (*s == 'X' ? 'A' : 'a') + d - 10);
                }
                hex[8] = 0;
                for (int k = 0; k < 8 && dst < end; k++) *dst++ = hex[k];
                s++;
            } else { s++; }
        }
        *dst = 0;
        p = tmp; while (*p && len < 126) buf[len++] = *p++;
    }
    va_end(args);
    p = "\r\n"; while (*p && len < 126) buf[len++] = *p++;
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, (uint16_t)len, 100);
}
#ifdef FW_DEBUG_ENABLE
#define FW_DBG(fmt, ...) fw_dbg_impl(__func__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define FW_DBG(fmt, ...) ((void)0)
#endif


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

    FW_DBG("fw_info_load: writing first record to slot 0...");

    uint32_t *src = (uint32_t *)&g_fw_info;
    uint32_t addr = FW_INFO_ADDR;
    for (uint32_t i = 0; i < sizeof(fw_info_t) / 4; i++)
    {
        if (boot_flash_write_word(addr, *src) != 0)
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
    uint32_t last_idx;
    fw_info_scan_last(&last_idx);

    uint32_t write_idx = last_idx + 1;
    if (write_idx >= FW_INFO_SLOT_COUNT)
    {
        FW_DBG("fw_info_save: sector full, erasing and wrapping to 0");
        if (boot_erase_sector(FW_INFO_SECTOR) != 0)
        {
            FW_DBG("fw_info_save: erase FAILED");
            return -1;
        }
        write_idx = 0;
    }

    const fw_info_t *base = (const fw_info_t *)FW_INFO_ADDR;
    if (base[write_idx].magic != 0xFFFFFFFF)
    {
        FW_DBG("fw_info_save: target index %u not empty, trying next", write_idx);
        write_idx++;
        if (write_idx >= FW_INFO_SLOT_COUNT)
        {
            FW_DBG("fw_info_save: sector full, erasing");
            if (boot_erase_sector(FW_INFO_SECTOR) != 0)
            {
                FW_DBG("fw_info_save: erase FAILED");
                return -1;
            }
            write_idx = 0;
        }
    }

    g_fw_info.crc32 = boot_crc32((const uint8_t *)&g_fw_info,
                                  sizeof(fw_info_t) - 4);

    uint32_t *src = (uint32_t *)&g_fw_info;
    uint32_t addr = FW_INFO_ADDR + write_idx * sizeof(fw_info_t);
    for (uint32_t i = 0; i < sizeof(fw_info_t) / 4; i++)
    {
        if (boot_flash_write_word(addr, *src) != 0)
        {
            FW_DBG("fw_info_save: write FAILED at slot %u", write_idx);
            return -1;
        }
        addr += 4;
        src++;
    }

    FW_DBG("fw_info_save: written to index %u", write_idx);
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
