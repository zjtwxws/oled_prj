/**
 * @file    app_fw_info.h
 * @brief   APP 侧固件信息访问接口 (只读 fw_info 结构 + 写 ota_request)
 */

#ifndef __APP_FW_INFO_H
#define __APP_FW_INFO_H

#include <stdint.h>

/* fw_info 在 S10 的地址 */
#define APP_FW_INFO_ADDR    0x080C0000UL

/* fw_info_t 内部 ota_request 字段偏移 (13 字节) */
#define APP_FW_INFO_OTA_REQ_OFFSET  13

/* Slot 基地址 */
#ifndef APP_SLOT_B
#define APP_VTOR_ADDR       0x08008000UL
#else
#define APP_VTOR_ADDR       0x08060000UL
#endif

/**
 * @brief  设置 ota_request = 1, 触发下次启动进入 Bootloader 更新模式
 * @return 0=成功, 非0=失败
 */
int app_fw_info_set_ota_request(void);

#endif /* __APP_FW_INFO_H */
