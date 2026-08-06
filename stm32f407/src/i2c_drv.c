/**
 * @file    i2c_drv.c
 * @brief   I²C 驱动实现, 封装 STM32 HAL I²C
 *
 * 超时由 I2C_TIMEOUT_DEFAULT 控制 (默认 100ms),
 * 避免 HAL_MAX_DELAY 导致总线挂死后永久阻塞。
 */

#include "i2c_drv.h"
#include "stm32f4xx_hal.h"

#ifndef I2C_TIMEOUT_DEFAULT
#define I2C_TIMEOUT_DEFAULT  100  /* I2C 操作超时 (ms)，避免总线异常时永久阻塞 */
#endif

/* I2C 句柄指针，由 i2c_drv_init() 从 CubeMX 生成的 hi2c2 赋值 */
static I2C_HandleTypeDef *p_i2c = NULL;

void i2c_drv_init(void *hi2c)
{
    p_i2c = (I2C_HandleTypeDef *)hi2c;
}

int i2c_drv_write_reg(uint8_t dev_addr, uint8_t reg, const uint8_t *data, uint16_t len)
{
    if (p_i2c == NULL) return -1;

    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(
        p_i2c,
        (uint16_t)(dev_addr << 1),
        (uint16_t)reg,
        I2C_MEMADD_SIZE_8BIT,
        (uint8_t *)(uintptr_t)data,
        len,
        I2C_TIMEOUT_DEFAULT
    );

    if (status == HAL_TIMEOUT) return -2;
    return (status == HAL_OK) ? 0 : -1;
}

int i2c_drv_is_ready(uint8_t dev_addr)
{
    if (p_i2c == NULL) return -1;

    HAL_StatusTypeDef status = HAL_I2C_IsDeviceReady(
        p_i2c,
        (uint16_t)(dev_addr << 1),
        3,
        I2C_TIMEOUT_DEFAULT
    );
    return (status == HAL_OK) ? 0 : -1;
}
