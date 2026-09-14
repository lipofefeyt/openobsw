/**
 * @file src/hal/stm32h7/qmc5883l.c
 * @brief QMC5883L 3-axis magnetometer driver (I2C address 0x0D).
 *
 * Register map (QST QMC5883L datasheet rev 1.0):
 *   0x00–0x05  DX_L, DX_H, DY_L, DY_H, DZ_L, DZ_H  (16-bit signed LE)
 *   0x06       Status: DRDY[0], OVL[1], DOR[2]
 *   0x09       Control 1: MODE[1:0], ODR[3:2], RNG[5:4], OSR[7:6]
 *   0x0A       Control 2: SOFT_RST[7], ROL_PNT[6], INT_ENB[0]
 *   0x0B       SET/RESET Period (recommended: 0x01)
 *   0x0D       Chip ID (should read 0xFF)
 *
 * Configuration applied by obsw_qmc5883l_init():
 *   OSR=512(00), RNG=2G(00), ODR=10Hz(00), MODE=continuous(01) → ctrl1=0x01
 *   Soft reset on init, then SET/RESET period = 0x01.
 */

#include "obsw/hal/stm32h7/qmc5883l.h"
#include "obsw/hal/stm32h7/i2c.h"

#define QMC_ADDR      0x0DU   /* 7-bit I2C address */
#define QMC_REG_DATA  0x00U   /* first of 6 data bytes */
#define QMC_REG_STAT  0x06U
#define QMC_REG_CTRL1 0x09U
#define QMC_REG_CTRL2 0x0AU
#define QMC_REG_SETRST 0x0BU
#define QMC_REG_ID    0x0DU

#define QMC_CTRL1_VAL 0x01U   /* MODE=cont, ODR=10Hz, RNG=2G, OSR=512 */
#define QMC_CTRL2_RST 0x80U   /* SOFT_RST */
#define QMC_ID_EXPECTED 0xFFU
#define QMC_STAT_DRDY (1U << 0)

/* Scale: 12000 LSB/Gauss at 2 Gauss range */
#define QMC_SCALE_GAUSS (1.0f / 12000.0f)

bool obsw_qmc5883l_init(void)
{
    uint8_t id = 0;

    /* Soft reset */
    if (!obsw_i2c1_write_reg(QMC_ADDR, QMC_REG_CTRL2, QMC_CTRL2_RST))
        return false;

    /* Check chip ID — distinguishes QMC5883L from other devices on the bus */
    if (!obsw_i2c1_read_regs(QMC_ADDR, QMC_REG_ID, &id, 1))
        return false;
    if (id != QMC_ID_EXPECTED)
        return false;

    /* SET/RESET period — QST recommends 0x01 */
    if (!obsw_i2c1_write_reg(QMC_ADDR, QMC_REG_SETRST, 0x01U))
        return false;

    /* Control 2: normal operation (no interrupt, no roll-over pointer) */
    if (!obsw_i2c1_write_reg(QMC_ADDR, QMC_REG_CTRL2, 0x00U))
        return false;

    /* Control 1: MODE=continuous, ODR=10 Hz, RNG=2G, OSR=512 */
    if (!obsw_i2c1_write_reg(QMC_ADDR, QMC_REG_CTRL1, QMC_CTRL1_VAL))
        return false;

    return true;
}

bool obsw_qmc5883l_read(float b[3])
{
    uint8_t stat = 0;
    if (!obsw_i2c1_read_regs(QMC_ADDR, QMC_REG_STAT, &stat, 1))
        return false;
    if (!(stat & QMC_STAT_DRDY))
        return false;

    uint8_t raw[6];
    if (!obsw_i2c1_read_regs(QMC_ADDR, QMC_REG_DATA, raw, 6))
        return false;

    /* Each axis: signed 16-bit little-endian */
    int16_t x = (int16_t)((uint16_t)raw[1] << 8 | raw[0]);
    int16_t y = (int16_t)((uint16_t)raw[3] << 8 | raw[2]);
    int16_t z = (int16_t)((uint16_t)raw[5] << 8 | raw[4]);

    b[0] = (float)x * QMC_SCALE_GAUSS;
    b[1] = (float)y * QMC_SCALE_GAUSS;
    b[2] = (float)z * QMC_SCALE_GAUSS;
    return true;
}
