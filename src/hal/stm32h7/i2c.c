/**
 * @file src/hal/stm32h7/i2c.c
 * @brief STM32H750 I2C1 polling master HAL.
 *
 * I2C1 on APB1L bus.  Pins (GPIOB, AF4, open-drain):
 *   SCL  PB8    SDA  PB9
 *
 * Timing: HSI 64 MHz kernel clock, standard mode 100 kHz.
 *   PRESC=3 → tPRESC = 62.5 ns
 *   SCLH=63 → tHIGH  = 64 × 62.5 =  4000 ns  (spec ≥ 4000 ns)
 *   SCLL=95 → tLOW   = 96 × 62.5 =  6000 ns  (spec ≥ 4700 ns)
 *   tSCL = 160 × 62.5 = 10000 ns → 100 kHz
 *   SCLDEL=3 → tSCLDEL = 4 × 62.5 = 250 ns   (spec ≥ 250 ns)
 *   SDADEL=2 → tSDADEL = 2 × 62.5 = 125 ns   (spec 0–3450 ns)
 *   TIMINGR = 0x30323F5F
 *
 * Bit positions verified against RM0433 rev 7 and stm32h750xx.h CMSIS header.
 */

#include "obsw/hal/stm32h7/i2c.h"
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Register map                                                         */
/* ------------------------------------------------------------------ */

#define RCC_BASE     0x58024400UL
#define GPIOB_BASE   0x58020400UL
#define I2C1_BASE    0x40005400UL

/* RCC */
#define RCC_AHB4ENR   (*(volatile uint32_t *)(RCC_BASE + 0x0E0U)) /* GPIOBEN bit 1  */
#define RCC_APB1LENR  (*(volatile uint32_t *)(RCC_BASE + 0x0E8U)) /* I2C1EN  bit 21 */

/* GPIOB */
#define GPIOB_MODER   (*(volatile uint32_t *)(GPIOB_BASE + 0x00U))
#define GPIOB_OTYPER  (*(volatile uint32_t *)(GPIOB_BASE + 0x04U))
#define GPIOB_OSPEEDR (*(volatile uint32_t *)(GPIOB_BASE + 0x08U))
#define GPIOB_PUPDR   (*(volatile uint32_t *)(GPIOB_BASE + 0x0CU))
#define GPIOB_AFRH    (*(volatile uint32_t *)(GPIOB_BASE + 0x24U))

/* I2C1 */
#define I2C1_CR1    (*(volatile uint32_t *)(I2C1_BASE + 0x00U))
#define I2C1_CR2    (*(volatile uint32_t *)(I2C1_BASE + 0x04U))
#define I2C1_TIMINGR (*(volatile uint32_t *)(I2C1_BASE + 0x10U))
#define I2C1_ISR    (*(volatile uint32_t *)(I2C1_BASE + 0x18U))
#define I2C1_ICR    (*(volatile uint32_t *)(I2C1_BASE + 0x1CU))
#define I2C1_RXDR   (*(volatile uint32_t *)(I2C1_BASE + 0x24U))
#define I2C1_TXDR   (*(volatile uint32_t *)(I2C1_BASE + 0x28U))

/* I2C1_ISR bits */
#define I2C_ISR_TXE   (1U << 0)   /* TX data register empty (may write before START) */
#define I2C_ISR_TXIS  (1U << 1)   /* TX interrupt status — ready for next byte       */
#define I2C_ISR_RXNE  (1U << 2)   /* RX data register not empty                      */
#define I2C_ISR_NACKF (1U << 4)   /* NACK received                                   */
#define I2C_ISR_STOPF (1U << 5)   /* STOP condition detected (AUTOEND)               */
#define I2C_ISR_TC    (1U << 6)   /* Transfer complete (no AUTOEND)                  */
#define I2C_ISR_BUSY  (1U << 15)

/* I2C1_ICR bits */
#define I2C_ICR_NACKCF (1U << 4)
#define I2C_ICR_STOPCF (1U << 5)

/* CR2 field helpers */
#define I2C_CR2_SADD(a)    ((uint32_t)((a) << 1))   /* 7-bit addr into SADD[7:1] */
#define I2C_CR2_NBYTES(n)  ((uint32_t)((n) << 16))
#define I2C_CR2_RD         (1U << 10)   /* RD_WRN — read direction */
#define I2C_CR2_START      (1U << 13)
#define I2C_CR2_STOP       (1U << 14)
#define I2C_CR2_AUTOEND    (1U << 25)

#define I2C_TIMINGR_VAL    0x30323F5FUL

/* Timeout: ~1.6 ms at 64 MHz — well above worst-case 9-bit I2C byte at 100 kHz */
#define I2C_TIMEOUT 100000U

/* ------------------------------------------------------------------ */
/* Init                                                                 */
/* ------------------------------------------------------------------ */

void obsw_i2c1_init(void)
{
    /* 1. Enable GPIOB and I2C1 clocks */
    RCC_AHB4ENR  |= (1U << 1);   /* GPIOBEN */
    RCC_APB1LENR |= (1U << 21);  /* I2C1EN  */

    /* 2. PB8 = I2C1_SCL, PB9 = I2C1_SDA (AF4, open-drain, pull-up, medium speed)
     *
     * MODER: AF mode = 10 for PB8 [17:16] and PB9 [19:18]
     * OTYPER: open-drain = 1 for bits 8 and 9
     * OSPEEDR: medium speed = 01 for PB8 [17:16] and PB9 [19:18]
     * PUPDR: pull-up = 01 for PB8 [17:16] and PB9 [19:18]
     * AFRH: AF4 into PB8 [3:0] and PB9 [7:4] */
    GPIOB_MODER   &= ~((3U << 16) | (3U << 18));
    GPIOB_MODER   |=  ((2U << 16) | (2U << 18)); /* AF */
    GPIOB_OTYPER  |=  ((1U << 8)  | (1U << 9));  /* open-drain */
    GPIOB_OSPEEDR &= ~((3U << 16) | (3U << 18));
    GPIOB_OSPEEDR |=  ((1U << 16) | (1U << 18)); /* medium speed */
    GPIOB_PUPDR   &= ~((3U << 16) | (3U << 18));
    GPIOB_PUPDR   |=  ((1U << 16) | (1U << 18)); /* pull-up */
    GPIOB_AFRH    &= ~(0xFFU);
    GPIOB_AFRH    |=  (4U | (4U << 4));           /* AF4 for PB8 and PB9 */

    /* 3. Configure I2C1 — must be done with PE=0 */
    I2C1_CR1     = 0U;                /* PE=0, disable peripheral while configuring */
    I2C1_TIMINGR = I2C_TIMINGR_VAL;  /* 100 kHz at HSI 64 MHz */
    I2C1_CR1     = (1U << 0);        /* PE=1, enable */
}

/* ------------------------------------------------------------------ */
/* Private helpers                                                      */
/* ------------------------------------------------------------------ */

static bool wait_flag(uint32_t flag, uint32_t clear_on_nack)
{
    for (uint32_t t = 0; t < I2C_TIMEOUT; t++) {
        uint32_t isr = I2C1_ISR;
        if (isr & I2C_ISR_NACKF) {
            I2C1_ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF;
            (void)clear_on_nack;
            return false;
        }
        if (isr & flag)
            return true;
    }
    return false; /* timeout */
}

/* ------------------------------------------------------------------ */
/* Public API                                                            */
/* ------------------------------------------------------------------ */

bool obsw_i2c1_write_reg(uint8_t addr7, uint8_t reg, uint8_t val)
{
    /* Wait for bus free */
    for (uint32_t t = 0; t < I2C_TIMEOUT; t++)
        if (!(I2C1_ISR & I2C_ISR_BUSY)) break;

    I2C1_ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF;

    /* 2-byte write: register address then value, with AUTOEND */
    I2C1_CR2 = I2C_CR2_SADD(addr7)
              | I2C_CR2_NBYTES(2)
              | I2C_CR2_AUTOEND
              | I2C_CR2_START;

    if (!wait_flag(I2C_ISR_TXIS, 0)) return false;
    I2C1_TXDR = reg;

    if (!wait_flag(I2C_ISR_TXIS, 0)) return false;
    I2C1_TXDR = val;

    if (!wait_flag(I2C_ISR_STOPF, 0)) return false;
    I2C1_ICR = I2C_ICR_STOPCF;
    return true;
}

bool obsw_i2c1_read_regs(uint8_t addr7, uint8_t reg, uint8_t *buf, uint8_t len)
{
    if (!buf || !len) return false;

    /* Wait for bus free */
    for (uint32_t t = 0; t < I2C_TIMEOUT; t++)
        if (!(I2C1_ISR & I2C_ISR_BUSY)) break;

    I2C1_ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF;

    /* Phase 1: write the register address (no AUTOEND → repeated start) */
    I2C1_CR2 = I2C_CR2_SADD(addr7)
              | I2C_CR2_NBYTES(1)
              | I2C_CR2_START;         /* no AUTOEND, no RD */

    if (!wait_flag(I2C_ISR_TXIS, 0)) return false;
    I2C1_TXDR = reg;
    if (!wait_flag(I2C_ISR_TC, 0)) return false;  /* TC, not STOPF — bus stays busy */

    /* Phase 2: repeated start, read len bytes */
    I2C1_CR2 = I2C_CR2_SADD(addr7)
              | I2C_CR2_NBYTES(len)
              | I2C_CR2_RD
              | I2C_CR2_AUTOEND
              | I2C_CR2_START;

    for (uint8_t i = 0; i < len; i++) {
        if (!wait_flag(I2C_ISR_RXNE, 0)) return false;
        buf[i] = (uint8_t)I2C1_RXDR;
    }

    if (!wait_flag(I2C_ISR_STOPF, 0)) return false;
    I2C1_ICR = I2C_ICR_STOPCF;
    return true;
}
