/**
 * @file    ota_req.c
 * @brief   APP 侧 OTA 请求 — 写入 NOINIT SRAM 区域通知 Bootloader
 *
 * 地址 0x2001BFF0 在 scatter 文件中已从 RW_IRAM1 排除,
 * 系统复位后 __scatterload 不会清零此区域, 内容得以保持。
 */

#include "ota_req.h"

/**
 * @brief  设置 OTA 升级请求标志
 * @return 0=成功
 */
int ota_req_set_update(void)
{
    volatile ota_req_t *req = (volatile ota_req_t *)OTA_REQ_ADDR;

    req->magic   = OTA_REQ_MAGIC;
    req->request = OTA_REQ_UPDATE;
    req->slot    = 0;
    req->reserved = 0;

    return 0;
}
