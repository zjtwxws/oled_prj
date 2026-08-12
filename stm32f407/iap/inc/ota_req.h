/**
 * @file    ota_req.h
 * @brief   OTA 升级请求 — 通过 SRAM NOINIT 区域在 APP 与 Bootloader 间传递升级标志
 *
 * 原理: STM32F407 SRAM 在系统复位 (NVIC_SystemReset) 后内容保持, 但上电复位后会丢失。
 *       这与 OTA 请求的语义匹配: 断电后重新上电应正常启动 APP, 不需要进入升级模式。
 *
 * 地址选择: 0x2001BFF0 位于 SRAM1 末端 16 字节, 不会与栈溢出冲突。
 *           Bootloader 和 APP 的 scatter 文件中均缩小了 RW_IRAM1 以保留此区域。
 *
 * 参考: MCUBoot / OpenBLT / Zephyr 均使用此方案 (retained memory / .noinit section)
 */

#ifndef __OTA_REQ_H
#define __OTA_REQ_H

#include <stdint.h>

/* ---- NOINIT 区域地址 ---- */
/** NOINIT 区域基地址 (SRAM1 末端 16 字节) */
#define OTA_REQ_ADDR        0x2001BFF0UL

/* ---- 魔数 ---- */
/** 有效请求魔数: 任意非 0x00/0xFF 的唯一值, 降低误触发概率 */
#define OTA_REQ_MAGIC       0x4F544152UL  /* "OTAR" */

/* ---- 请求类型 ---- */
#define OTA_REQ_NONE        0x00000000UL  /** 无请求 (擦除态) */
#define OTA_REQ_UPDATE      0x00000001UL  /** APP 请求进入固件升级模式 */

/* ---- NOINIT 结构体 ---- */
typedef struct
{
    uint32_t magic;     /** 魔数 OTA_REQ_MAGIC — 防误触发 */
    uint32_t request;   /** 请求类型 (OTA_REQ_NONE / OTA_REQ_UPDATE) */
    uint32_t slot;      /** 目标槽位 (0=A, 1=B, 预留) */
    uint32_t reserved;  /** 保留扩展 */
} ota_req_t;

/* ---- API (APP 侧) ---- */

/**
 * @brief  设置 OTA 升级请求标志 (APP 侧调用)
 * @return 0=成功
 */
int ota_req_set_update(void);

/* ---- API (Bootloader 侧) ---- */

/**
 * @brief  检查是否有 OTA 升级请求 (Bootloader 侧调用)
 * @return 1=有请求, 0=无请求
 */
int ota_req_is_update(void);

/**
 * @brief  清除 OTA 升级请求标志 (Bootloader 进入升级模式后调用)
 */
void ota_req_clear(void);

#endif /* __OTA_REQ_H */
