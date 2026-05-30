/**
 * @file src/hal/stm32h7/spi.c
 * @brief STM32H750 SPI4 HAL — half-duplex TX for ST7735R LCD.
 *
 * SPI4 on APB2 bus. Pins (all on GPIOE, AF6):
 *   SCK  PE12   MOSI PE14
 *
 * Clock: HSI 32 MHz / fPCLK/8 → ~4 MHz SPI clock (ST7735R max: 20 MHz).
 * Mode: SPI master, half-duplex TX, 8-bit, CPOL=0 CPHA=0, software CS.
 *
 * CS (PE11), DC (PE13), RST (PE3), BL (PE10) are GPIO outputs configured
 * by the LCD driver — GPIOE clock is enabled here so the LCD driver need
 * not duplicate that step.
 */

#include "obsw/hal/stm32h7/spi.h"
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Register map                                                         */
/* ------------------------------------------------------------------ */

#define RCC_BASE     0x58024400UL
#define GPIOE_BASE   0x58021000UL
#define SPI4_BASE    0x40013400UL

/* RCC */
#define RCC_AHB4ENR  (*(volatile uint32_t *)(RCC_BASE + 0x0E0U)) /* GPIOEEN bit 4 */
#define RCC_APB2ENR  (*(volatile uint32_t *)(RCC_BASE + 0x0F0U)) /* SPI4EN  bit 13 */

/* GPIOE */
#define GPIOE_MODER   (*(volatile uint32_t *)(GPIOE_BASE + 0x00U))
#define GPIOE_OSPEEDR (*(volatile uint32_t *)(GPIOE_BASE + 0x08U))
#define GPIOE_AFRH    (*(volatile uint32_t *)(GPIOE_BASE + 0x24U))

/* SPI4 */
#define SPI4_CR1  (*(volatile uint32_t *)(SPI4_BASE + 0x00U))
#define SPI4_CR2  (*(volatile uint32_t *)(SPI4_BASE + 0x04U))
#define SPI4_CFG1 (*(volatile uint32_t *)(SPI4_BASE + 0x08U))
#define SPI4_CFG2 (*(volatile uint32_t *)(SPI4_BASE + 0x0CU))
#define SPI4_SR   (*(volatile uint32_t *)(SPI4_BASE + 0x14U))
#define SPI4_IFCR (*(volatile uint32_t *)(SPI4_BASE + 0x18U))
/* Byte-wide alias for 8-bit TXDR writes (DSIZE=8 requires byte access) */
#define SPI4_TXDR8 (*(volatile uint8_t  *)(SPI4_BASE + 0x20U))

/* SPI4_SR bits */
#define SPI_SR_TXP  (1U << 1)   /* TX packet space available */
#define SPI_SR_EOT  (1U << 3)   /* End of transfer (TSIZE frames sent) */

/* SPI4_CR1 bits */
#define SPI_CR1_SPE    (1U << 0)
#define SPI_CR1_CSTART (1U << 8)
#define SPI_CR1_HDDIR  (1U << 10) /* Half-duplex direction: 1=TX */

/* ------------------------------------------------------------------ */
/* Init                                                                 */
/* ------------------------------------------------------------------ */

void obsw_spi4_init(void)
{
    /* 1. Enable peripheral clocks */
    RCC_AHB4ENR |= (1U << 4);   /* GPIOEEN */
    RCC_APB2ENR  |= (1U << 13); /* SPI4EN  */

    /* 2. PE12 = SPI4_SCK (AF6), PE14 = SPI4_MOSI (AF6)
     *    GPIOE is shared with LCD CS/DC/RST/BL — those pins are
     *    configured as GPIO outputs by the LCD driver. */
    GPIOE_MODER   &= ~((3U << 24) | (3U << 28)); /* clear PE12, PE14 */
    GPIOE_MODER   |=  ((2U << 24) | (2U << 28)); /* AF mode */
    GPIOE_OSPEEDR |=  ((3U << 24) | (3U << 28)); /* very high speed */
    GPIOE_AFRH    &= ~((0xFU << 16) | (0xFU << 24));
    GPIOE_AFRH    |=  ((6U   << 16) | (6U   << 24)); /* AF6 */

    /* 3. Configure SPI4
     *
     * CFG1: DSIZE=7 (8-bit frames), FTHLV=0 (1-frame FIFO threshold),
     *        MBR=010 (fPCLK/8 → ~4 MHz at APB2=32 MHz)
     *
     * CFG2: COMM[18:17]=0b10 → half-duplex (direction via HDDIR in CR1),
     *        MASTER=1, SSM=1 (software CS — we drive PE11 manually),
     *        CPOL=0, CPHA=0 → SPI mode 0
     */
    SPI4_CFG1 = (7U  <<  0)   /* DSIZE */
               | (2U  << 26);  /* MBR   */

    SPI4_CFG2 = (1U  << 18)   /* COMM[1] → half-duplex */
               | (1U  << 22)   /* MASTER  */
               | (1U  << 26);  /* SSM     */
}

/* ------------------------------------------------------------------ */
/* TX                                                                   */
/* ------------------------------------------------------------------ */

void obsw_spi4_write(const uint8_t *buf, uint16_t len)
{
    if (!len) return;

    SPI4_CR2 = len;                              /* TSIZE = byte count  */
    SPI4_CR1 = SPI_CR1_HDDIR | SPI_CR1_SPE;     /* TX direction + enable */
    SPI4_CR1 |= SPI_CR1_CSTART;                 /* kick off transfer    */

    for (uint16_t i = 0; i < len; i++) {
        while (!(SPI4_SR & SPI_SR_TXP));         /* wait for space       */
        SPI4_TXDR8 = buf[i];
    }
    while (!(SPI4_SR & SPI_SR_EOT));             /* wait for last bit out */

    SPI4_IFCR = SPI_SR_EOT | (1U << 4);         /* clear EOT + TXTF     */
    SPI4_CR1  = 0;                               /* disable SPI          */
}

void obsw_spi4_write_byte(uint8_t byte)
{
    obsw_spi4_write(&byte, 1);
}
