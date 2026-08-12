/**
 * @file    boot_ota_req.c
 * @brief   OTA 升级请求 — SRAM NOINIT 区域读写 (Bootloader 侧)
 */

#include "ota_req.h"

/**
 * @brief  检查是否有 OTA 升级请求
 * @return 1=有请求, 0=无请求
 */
int ota_req_is_update(void)
{
    volatile ota_req_t *req = (volatile ota_req_t *)OTA_REQ_ADDR;
    return (req->magic == OTA_REQ_MAGIC && req->request == OTA_REQ_UPDATE) ? 1 : 0;
}

/**
 * @brief  清除 OTA 升级请求标志
 */
void ota_req_clear(void)
{
    volatile ota_req_t *req = (volatile ota_req_t *)OTA_REQ_ADDR;
    req->magic   = 0;
    req->request = OTA_REQ_NONE;
    req->slot    = 0;
    req->reserved = 0;
}
