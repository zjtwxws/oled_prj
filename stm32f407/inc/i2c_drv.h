/**
 * @file    i2c_drv.h
 * @brief   I²C 驱动抽象层 (封装 HAL)
 */

#ifndef __I2C_DRV_H
#define __I2C_DRV_H

#include <stdint.h>

void i2c_drv_init(void *hi2c);
int i2c_drv_write_reg(uint8_t dev_addr, uint8_t reg, const uint8_t *data, uint16_t len);
int i2c_drv_is_ready(uint8_t dev_addr);

#endif
