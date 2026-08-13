/**
 * @file    sys_config.c
 * @brief   系统配置存储实现 (STM32 内部 Flash)
 *
 * 用户需确认 Flash 分区规划:
 * STM32F407VG Flash: 1024KB, Sector 0~11, Sector 11(0x080E0000)为最后128KB.
 * 代码区(Bootloader+App): Sector 0~7(0x08000000~0x080BFFFF, 768KB) 足够容纳本项目代码+字模.
 * 配置区: Sector 11(0x080E0000~0x080FFFFF) 独立使用, 不与代码重叠.
 * 若代码超过768KB, 需将配置区前移或使用更小的配置区占位.
 *
 * 安全建议: 使用 Sector 10(0x080C0000) 作为配置区, Sector 11 作为 OTA 预留区.
 *           此处继续用 Sector 11, 代码应确保不超过 Sector 0~9 (896KB = 前 7 个 128KB + 中间 4 个 64KB).
 *           STM32F407VG 有 5 个 128KB + 2 个 64KB + 4 个 128KB + 1 个 128KB 不等长扇区,
 *           实际 Sector 7 已经是 0x08060000, Sector 11 是最后128KB.
 */

#include "sys_config.h"
#include "stm32f4xx_hal.h"
#include <string.h>

#define CONFIG_FLASH_ADDR   0x080E0000UL   /* Sector 11 起始地址 */
#define CONFIG_FLASH_SECTOR FLASH_SECTOR_11  /* 配置存储扇区: Sector 11 (128KB) */
#define CONFIG_MAGIC        0x4F4C4544UL          /* 魔数 "OLED" (ASCII)，用于验证配置有效性 */

/* RAM 中的配置缓存，上电后从 Flash 加载，运行时所有读写操作此副本。 */
static sys_config_t config;

/* 简单 CRC-32 校验（IEEE 802.3 反射多项式 0xEDB88320），用于验证 Flash 中配置数据的完整性。 */
/**
 * @brief  计算 CRC-32 校验值（IEEE 802.3）
 * @param  data 参数说明
 * @param  len 参数说明
 * @return 返回值说明
 * @date   2026-08-07
 */
static uint32_t crc32_simple(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
        {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

/**
 * @brief  初始化系统配置（从 Flash 加载，无效时使用默认值）
 * @date   2026-08-07
 */
int sys_config_init(void)
{
    /* 读取 Flash 中的配置 */
    const sys_config_t *flash_cfg = (const sys_config_t *)CONFIG_FLASH_ADDR;

    if (flash_cfg->magic == CONFIG_MAGIC)
    {
        /* 校验 CRC (不包含 crc32 字段自身) */
        uint32_t calc_crc = crc32_simple((const uint8_t *)flash_cfg,
                                          sizeof(sys_config_t) - 4);
        if (calc_crc == flash_cfg->crc32)
        {
            /* 有效配置, 复制到 RAM */
            memcpy(&config, flash_cfg, sizeof(sys_config_t));
            return 0;
        }
    }

    /* 无效或无配置, 使用默认值 */
    memset(&config, 0, sizeof(sys_config_t));
    config.magic = CONFIG_MAGIC;
    strcpy(config.boot_text, "欢迎进入系统");

    /* 计算并保存 */
    config.crc32 = crc32_simple((const uint8_t *)&config, sizeof(sys_config_t) - 4);
    sys_config_save();

    return 1;  /* 使用了默认值 */
}

/**
 * @brief  保存系统配置到 Flash
 * @date   2026-08-07
 */
int sys_config_save(void)
{
    HAL_FLASH_Unlock();

    /* 擦除扇区 */
    
    /* 擦除 Sector 11 (0x080E0000, 128KB)，擦除后该扇区全为 0xFF */
    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error;

    erase_init.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase_init.Sector       = CONFIG_FLASH_SECTOR;
    erase_init.NbSectors    = 1;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    if (HAL_FLASHEx_Erase(&erase_init, &sector_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return -1;
    }

    /* 按字(4B)写入配置 */
    uint32_t *src = (uint32_t *)&config;
    uint32_t  addr = CONFIG_FLASH_ADDR;
    uint32_t  words = sizeof(sys_config_t) / 4;

    for (uint32_t i = 0; i < words; i++)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, src[i]) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return -1;
        }

        if (*((volatile uint32_t *)addr) != src[i])
        {
            HAL_FLASH_Lock();
            return -1;
        }

        addr += 4;
    }

    HAL_FLASH_Lock();
    return 0;
}

/**
 * @brief  设置上电显示文字（内容变化时才擦写 Flash）
 * @param  text 参数说明
 * @date   2026-08-07
 */
void sys_config_set_boot_text(const char *text)
{
    if (text)
    {
        /* 仅内容变化时才擦写 Flash, 减少磨损 */
        if (strncmp(config.boot_text, text, SYS_CONFIG_BOOT_TEXT_LEN) == 0)
        {
            return;
        }
        strncpy(config.boot_text, text, SYS_CONFIG_BOOT_TEXT_LEN - 1);
        config.boot_text[SYS_CONFIG_BOOT_TEXT_LEN - 1] = '\0';
        config.crc32 = crc32_simple((const uint8_t *)&config, sizeof(sys_config_t) - 4);
        sys_config_save();
    }
}

/**
 * @brief  获取上电显示文字
 * @return 返回值说明
 * @date   2026-08-07
 */
const char* sys_config_get_boot_text(void)
{
    return config.boot_text;
}

/**
 * @brief  设置上电显示类型并保存到 Flash
 * @param  type 参数说明
 * @date   2026-08-07
 */
void sys_config_set_poweron_type(uint8_t type)
{
    if (type > 2)
    {
        return;
    }
    if (config.poweron_type == type)
    {
        return;
    }
    config.poweron_type = type;
    config.crc32 = crc32_simple((const uint8_t *)&config, sizeof(sys_config_t) - 4);
    sys_config_save();
}

/**
 * @brief  系统复位
 * @date   2026-08-07
 */
void sys_config_reset(void)
{
    NVIC_SystemReset();
}

/**
 * @brief  获取上电显示类型
 * @date   2026-08-07
 */
uint8_t sys_config_get_poweron_type(void)
{
    return config.poweron_type;
}
