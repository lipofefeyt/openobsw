/**
 * @file src/hal/stm32h7/hmc5883l.c
 * @brief Honeywell HMC5883L 3-axis magnetometer driver (I2C address 0x1E).
 *
 * Register map (Honeywell HMC5883L datasheet, Form #900405 Rev E):
 *   0x00  Config A: MA[6:5] averaging, DO[4:2] output rate, MS[1:0] bias
 *   0x01  Config B: GN[7:5] gain
 *   0x02  Mode:     MD[1:0] (00 = continuous)
 *   0x03..0x08  X_H, X_L, Z_H, Z_L, Y_H, Y_L  (16-bit signed, BIG-endian,
 *               note the X, Z, Y order)
 *   0x09  Status: RDY[0], LOCK[1]
 *   0x0A..0x0C  ID A/B/C = 'H', '4', '3'
 *
 * An axis that overflows the ADC range reads -4096 (0xF000).
 */

#include "obsw/hal/stm32h7/hmc5883l.h"
#include "obsw/hal/stm32h7/i2c.h"

#include <stdint.h>

#define HMC_ADDR      0x1EU
#define HMC_REG_CRA   0x00U
#define HMC_REG_CRB   0x01U
#define HMC_REG_MODE  0x02U
#define HMC_REG_DATA  0x03U
#define HMC_REG_STAT  0x09U
#define HMC_REG_ID    0x0AU

#define HMC_CRA_VAL   0x70U   /* MA=11 (8 avg), DO=100 (15 Hz), MS=00 (normal) */
#define HMC_CRB_VAL   0x20U   /* GN=001: +/-1.3 Ga, 1090 LSB/Ga */
#define HMC_MODE_CONT 0x00U
#define HMC_STAT_RDY  (1U << 0)
#define HMC_OVERFLOW  (-4096)

/* 1090 LSB/Ga and 1 Ga = 1e-4 T */
#define HMC_SCALE_T   (1.0e-4f / 1090.0f)

bool obsw_hmc5883l_init(void)
{
    uint8_t id[3] = {0, 0, 0};
    if (!obsw_i2c1_read_regs(HMC_ADDR, HMC_REG_ID, id, 3))
        return false;
    if (id[0] != 'H' || id[1] != '4' || id[2] != '3')
        return false;

    if (!obsw_i2c1_write_reg(HMC_ADDR, HMC_REG_CRA, HMC_CRA_VAL))
        return false;
    if (!obsw_i2c1_write_reg(HMC_ADDR, HMC_REG_CRB, HMC_CRB_VAL))
        return false;
    if (!obsw_i2c1_write_reg(HMC_ADDR, HMC_REG_MODE, HMC_MODE_CONT))
        return false;
    return true;
}

bool obsw_hmc5883l_read(float b[3])
{
    uint8_t stat = 0;
    if (!obsw_i2c1_read_regs(HMC_ADDR, HMC_REG_STAT, &stat, 1))
        return false;
    if (!(stat & HMC_STAT_RDY))
        return false;

    uint8_t raw[6];
    if (!obsw_i2c1_read_regs(HMC_ADDR, HMC_REG_DATA, raw, 6))
        return false;

    int16_t x = (int16_t)(((uint16_t)raw[0] << 8) | raw[1]);
    int16_t z = (int16_t)(((uint16_t)raw[2] << 8) | raw[3]);
    int16_t y = (int16_t)(((uint16_t)raw[4] << 8) | raw[5]);
    if (x == HMC_OVERFLOW || y == HMC_OVERFLOW || z == HMC_OVERFLOW)
        return false;

    b[0] = (float)x * HMC_SCALE_T;
    b[1] = (float)y * HMC_SCALE_T;
    b[2] = (float)z * HMC_SCALE_T;
    return true;
}
