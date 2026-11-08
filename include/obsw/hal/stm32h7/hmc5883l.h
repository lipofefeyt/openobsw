#ifndef OBSW_HAL_STM32H7_HMC5883L_H
#define OBSW_HAL_STM32H7_HMC5883L_H

/**
 * @file obsw/hal/stm32h7/hmc5883l.h
 * @brief Honeywell HMC5883L 3-axis magnetometer driver (GY-273 module).
 *
 * I2C address: 0x1E. Identified by ID registers 0x0A..0x0C = "H43".
 * Configured for continuous measurement, 15 Hz output, 8-sample averaging,
 * +/-1.3 Ga range (1090 LSB/Ga). 15 Hz guarantees a fresh sample on every
 * 10 Hz AOCS tick in src/task/aocs.c.
 *
 * Same interface as qmc5883l.h; the AOCS task uses whichever chip answers.
 */

#include <stdbool.h>

/* Identify and configure the chip. Returns false if no HMC5883L answers. */
bool obsw_hmc5883l_init(void);

/* Read the latest sample. b[0..2] = Bx, By, Bz in Tesla.
 * Returns false if no new sample is ready, an axis overflowed, or I2C fails. */
bool obsw_hmc5883l_read(float b[3]);

#endif /* OBSW_HAL_STM32H7_HMC5883L_H */
