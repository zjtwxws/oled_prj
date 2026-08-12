/**
 * @file    app_fw_info.h
 * @brief   APP 侧固件信息接口 — OTA 升级请求 (通过 SRAM NOINIT 区域传递)
 */

#ifndef __APP_FW_INFO_H
#define __APP_FW_INFO_H

/**
 * @brief  设置 OTA 升级请求标志 (写入 SRAM NOINIT 区域 0x2001BFF0)
 * @return 0=成功, 非0=失败
 */
int app_fw_info_set_ota_request(void);

#endif /* __APP_FW_INFO_H */
