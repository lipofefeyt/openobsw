#ifndef OBSW_HAL_STM32H7_I2C_H
#define OBSW_HAL_STM32H7_I2C_H

/**
 * @file obsw/hal/stm32h7/i2c.h
 * @brief I2C1 polling master HAL for STM32H750.
 *
 * I2C1 on APB1, 100 kHz standard mode.
 * Pins: SCL = PB8 (AF4), SDA = PB9 (AF4), open-drain.
 * The GY-273 module provides 4.7 kΩ pull-ups on both lines.
 */

#include <stdbool.h>
#include <stdint.h>

void obsw_i2c1_init(void);

/* Write a single register byte.  Returns false on NACK or timeout. */
bool obsw_i2c1_write_reg(uint8_t addr7, uint8_t reg, uint8_t val);

/* Write register address then repeated-start read of len bytes.
 * Returns false on NACK or timeout. */
bool obsw_i2c1_read_regs(uint8_t addr7, uint8_t reg, uint8_t *buf, uint8_t len);

#endif /* OBSW_HAL_STM32H7_I2C_H */
