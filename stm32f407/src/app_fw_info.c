/**
 * @file    app_fw_info.c
 * @brief   APP 侧固件信息操作 — OTA 升级请求通过 SRAM NOINIT 传递
 *
 * 原理: APP 将升级请求写入 SRAM 末端 NOINIT 区域 (0x2001BFF0),
 *       系统复位后 bootloader 在 SRAM 初始化前检查该区域。
 *       替代了原有的 Flash fw_info ota_request 方案 (该方案存在
 *       Flash 编程方向违规和 CRC 破坏两个致命缺陷)。
 */

#include "ota_req.h"

/**
 * @brief  设置 OTA 升级请求标志 (写入 SRAM NOINIT 区域)
 * @return 0=成功, 非0=失败
 * @date   2026-08-12
 */
int app_fw_info_set_ota_request(void)
{
    return ota_req_set_update();
}
