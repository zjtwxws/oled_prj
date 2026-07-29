/**
 * @file    i2c_drv.c
 * @brief   I²C 驱动实现, 封装 STM32 HAL I²C
 *
 * 用户需在 CubeMX 中配置好 I²C 外设,
 * 并在 i2c_drv_init() 中传入 &hi2cx 句柄。
 */

#include "i2c_drv.h"

/* ---- 依赖 STM32 HAL ---- */
#include "stm32f4xx_hal.h"

static I2C_HandleTypeDef *p_i2c = NULL;

void i2c_drv_init(void *hi2c)
{
    p_i2c = (I2C_HandleTypeDef *)hi2c;
}

int i2c_drv_write_reg(uint8_t dev_addr, uint8_t reg, const uint8_t *data, uint16_t len)
{
    if (p_i2c == NULL) return -1;

    /* HAL_I2C_Mem_Write:
     *   DevAddress: 7bit addr << 1
     *   MemAddress: register (0x00=cmd, 0x40=data for OLED)
     *   MemAddSize: I2C_MEMADD_SIZE_8BIT
     */
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(
        p_i2c,
        (uint16_t)(dev_addr << 1),
        (uint16_t)reg,
        I2C_MEMADD_SIZE_8BIT,
        (uint8_t *)data,
        len,
        HAL_MAX_DELAY
    );
    return (status == HAL_OK) ? 0 : -1;
}

int i2c_drv_is_ready(uint8_t dev_addr)
{
    if (p_i2c == NULL) return -1;

    HAL_StatusTypeDef status = HAL_I2C_IsDeviceReady(
        p_i2c,
        (uint16_t)(dev_addr << 1),
        3,             /* Trials */
        HAL_MAX_DELAY
    );
    return (status == HAL_OK) ? 0 : -1;
}
