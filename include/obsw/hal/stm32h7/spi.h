#ifndef OBSW_HAL_STM32H7_SPI_H
#define OBSW_HAL_STM32H7_SPI_H

#include <stdint.h>

/**
 * @file obsw/hal/stm32h7/spi.h
 * @brief SPI4 HAL for STM32H750 — LCD data path.
 *
 * SPI4, half-duplex TX, ~4 MHz (fPCLK/8 at HSI 32 MHz).
 * Pins: SCK=PE12 (AF6), MOSI=PE14 (AF6).
 * CS/DC/RST/BL are GPIO outputs managed by the LCD driver layer.
 */

void     obsw_spi4_init(void);
void     obsw_spi4_write(const uint8_t *buf, uint16_t len);
void     obsw_spi4_write_byte(uint8_t byte);

#endif /* OBSW_HAL_STM32H7_SPI_H */
