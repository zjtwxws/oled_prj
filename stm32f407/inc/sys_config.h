/**
 * @file    sys_config.h
 * @brief   系统配置管理 (Flash 参数存储)
 *
 * 使用 STM32 内部 Flash 扇区 11 (0x080E0000, Sector 11, 128KB) 的最后 16KB 存储配置.
 * 擦除单位为扇区(128KB), 写入按字(4B).
 */

#ifndef __SYS_CONFIG_H
#define __SYS_CONFIG_H

#include <stdint.h>

/* 默认上电文字最大长度 */
#define SYS_CONFIG_BOOT_TEXT_LEN  128

/* 上电显示类型 */
#define SYS_CONFIG_POWERON_WELCOME   0   /* 欢迎语 */
#define SYS_CONFIG_POWERON_LOGO      1   /* Logo 全屏位图 */
#define SYS_CONFIG_POWERON_BIGTEXT   2   /* 大号文字 */

/* 配置数据结构 (需保持 4 字节对齐) */
typedef struct {
    uint32_t magic;                              /* 魔数 0x4F4C4544 "OLED" */
    uint8_t  poweron_type;                       /* 上电显示类型 (0=欢迎语,1=Logo,2=大号文字) */
    uint8_t  _reserved[3];                       /* 对齐填充 */
    char     boot_text[SYS_CONFIG_BOOT_TEXT_LEN]; /* 上电默认文字 */
    uint32_t crc32;                              /* 简单校验 */
} sys_config_t;

int  sys_config_init(void);                /* 加载配置; 如无效则以默认值初始化 */
int  sys_config_save(void);                /* 保存配置到 Flash */
void sys_config_set_boot_text(const char *text);
const char* sys_config_get_boot_text(void);
void sys_config_set_poweron_type(uint8_t type);
uint8_t sys_config_get_poweron_type(void);

/** @brief ç³»ç»å¤ä½ (è°ç¨ NVIC_SystemResetï¼ä¸è¿å) */
void sys_config_reset(void);

#endif /* __SYS_CONFIG_H */
