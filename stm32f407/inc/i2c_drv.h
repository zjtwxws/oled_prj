/**
 * @file    i2c_drv.h
 * @brief   I²C 驱动抽象层 (封装 HAL)
 */

#ifndef __I2C_DRV_H
#define __I2C_DRV_H

#include <stdint.h>

/**
 * @brief  I²C 硬件初始化
 * @param  hi2c  HAL I2C 句柄指针 (如 &hi2c1)
 */
void i2c_drv_init(void *hi2c);

/**
 * @brief  写入设备寄存器 (OLED 命令/数据)
 * @param  dev_addr  7bit 从机地址
 * @param  reg       寄存器地址 (OLED: 0x00=命令, 0x40=数据)
 * @param  data      数据缓冲区
 * @param  len       数据长度
 * @return 0=成功
 */
int i2c_drv_write_reg(uint8_t dev_addr, uint8_t reg, const uint8_t *data, uint16_t len);

/**
 * @brief  检查 I²C 设备是否就绪
 * @return 0=就绪, 非0=未就绪
 */
int i2c_drv_is_ready(uint8_t dev_addr);

#endif /* __I2C_DRV_H */
