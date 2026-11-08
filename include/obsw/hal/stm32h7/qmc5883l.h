#ifndef OBSW_HAL_STM32H7_QMC5883L_H
#define OBSW_HAL_STM32H7_QMC5883L_H

/**
 * @file obsw/hal/stm32h7/qmc5883l.h
 * @brief QMC5883L 3-axis magnetometer driver.
 *
 * I2C address: 0x0D.
 * Configured for continuous measurement, 10 Hz ODR, 2 Gauss range,
 * OSR=512 — matched to the 10 Hz AOCS control tick in src/task/aocs.c.
 *
 * obsw_qmc5883l_read() returns the magnetic field in Tesla, the unit the
 * B-dot law, the AOCS HK and the host-sim sensor frame all use.
 * For the HMC5883L (GY-273 modules) see hmc5883l.h.
 */

#include <stdbool.h>

/* Configure the chip for continuous 10 Hz measurement.
 * Returns false if the device does not respond (NACK on I2C). */
bool obsw_qmc5883l_init(void);

/* Read the latest magnetic field sample.
 * b[0..2] = Bx, By, Bz in Tesla.
 * Returns false if DRDY is not set or an I2C error occurs. */
bool obsw_qmc5883l_read(float b[3]);

#endif /* OBSW_HAL_STM32H7_QMC5883L_H */
