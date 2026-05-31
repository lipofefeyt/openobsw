/**
 * @file src/hal/stm32h7/spi.c
 * @brief STM32H750 SPI4 HAL — simplex TX-only master for ST7735R LCD.
 *
 * SPI4 on APB2 bus. Pins (GPIOE, AF5):
 *   SCK PE12   MOSI PE14
 *
 * Clock: HSI 32 MHz → APB2 = 32 MHz, MBR=2 → fPCLK/8 = 4 MHz (ST7735R max 20 MHz).
 * Mode: simplex transmitter (COMM=11 in CFG2) — PE13 is GPIO DC output, not MISO.
 * SSM=1, SSI=1 prevents spurious MODF in master mode with software CS.
 *
 * CS (PE11), DC (PE13), BL (PE10) are GPIO outputs managed by lcd.c.
 * GPIOE clock is enabled here so lcd.c need not duplicate it.
 *
 * Bit positions verified against stm32h750xx.h CMSIS header (STMicroelectronics/cmsis_device_h7).
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
#define RCC_APB2RSTR (*(volatile uint32_t *)(RCC_BASE + 0x098U)) /* SPI4RST bit 13 */

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
/* Byte-wide aliases for 8-bit TXDR/RXDR access (DSIZE=8 requires byte access) */
#define SPI4_TXDR8 (*(volatile uint8_t  *)(SPI4_BASE + 0x20U))
#define SPI4_RXDR8 (*(volatile uint8_t  *)(SPI4_BASE + 0x30U))

/* SPI4_SR bits (CMSIS: RXP=0, TXP=1, DXP=2, EOT=3, MODF=9, TXC=12) */
#define SPI_SR_TXP  (1U << 1)   /* TX FIFO has space */
#define SPI_SR_EOT  (1U << 3)   /* End of transfer — all TSIZE frames shifted out */

/* SPI4_IFCR — write 1 to clear the corresponding SR flag */
#define SPI_IFCR_ALL  0x0FF8U   /* EOTC|TXTFC|UDRC|OVRC|CRCEC|TIFREC|MODFC|TSERFC|SUSPC */

/* SPI4_CR1 bits (CMSIS stm32h750xx.h: SPE=0, MASRX=8, CSTART=9, CSUSP=10, HDDIR=11, SSI=12) */
#define SPI_CR1_SPE    (1U << 0)
#define SPI_CR1_CSTART (1U << 9)  /* Master transfer start */
#define SPI_CR1_SSI    (1U << 12) /* Internal SS signal level — keep 1 to prevent MODF */

/* ------------------------------------------------------------------ */
/* Init                                                                 */
/* ------------------------------------------------------------------ */

void obsw_spi4_init(void)
{
    /* 1. Enable peripheral clocks, then pulse SPI4 reset to guarantee clean state.
     *    Without the reset, MODF can be spuriously set on first clock enable
     *    (slave/hardware-NSS default config briefly active), which blocks SPE later. */
    RCC_AHB4ENR |= (1U << 4);   /* GPIOEEN */
    RCC_APB2ENR  |= (1U << 13); /* SPI4EN  */
    RCC_APB2RSTR |= (1U << 13); /* assert SPI4RST */
    RCC_APB2RSTR &= ~(1U << 13); /* deassert — registers return to reset values */

    /* 2. PE12 = SPI4_SCK (AF5), PE14 = SPI4_MOSI (AF5)
     *    GPIOE is shared with LCD CS/DC/BL — those pins are
     *    configured as GPIO outputs by the LCD driver. */
    GPIOE_MODER   &= ~((3U << 24) | (3U << 28)); /* clear PE12, PE14 */
    GPIOE_MODER   |=  ((2U << 24) | (2U << 28)); /* AF mode */
    GPIOE_OSPEEDR |=  ((3U << 24) | (3U << 28)); /* very high speed */
    GPIOE_AFRH    &= ~((0xFU << 16) | (0xFU << 24));
    GPIOE_AFRH    |=  ((5U   << 16) | (5U   << 24)); /* AF5 = SPI4_SCK/MOSI */

    /* 3. Configure SPI4
     *
     * CFG1: DSIZE=7 (8-bit frames, value N = N+1 bits).
     *        MBR at CFG1[30:28]: value 2 → fPCLK/8 = 4 MHz.
     *
     * CFG2 (bit positions from CMSIS stm32h750xx.h):
     *   COMM[18:17] = 11 → simplex transmitter (PE13 is GPIO DC, not MISO)
     *   MASTER [22] = 1
     *   SSM    [26] = 1  (software NSS — we drive CS/PE11 manually)
     *   CPOL   [25] = 0, CPHA [24] = 0  → SPI mode 0
     *   LSBFRST[23] = 0  → MSB first
     */
    SPI4_CFG1 = (7U << 0)              /* DSIZE = 8-bit */
               | (2U << 28);           /* MBR[30:28] = 2 → fPCLK/8 */

    /* CR1 must have SSI=1 BEFORE CFG2 is written with MASTER=1+SSM=1.
     * If SSI=0 when MASTER transitions to 1, the H750 silicon clears MASTER
     * as part of MODF handling — even though only SPE is mentioned in RM0433. */
    SPI4_CR1  = SPI_CR1_SSI;

    /* COMM=00 (full-duplex): simplex-TX bits (COMM=11) cause EOT to never fire
     * on this STM32H750 silicon — SR shows RXP/DXP active despite COMM=11.
     * Full-duplex works: MISO (PE13) is GPIO-output-driven (DC signal), the
     * SPI samples it as garbage RX data which we discard via RXDR drain. */
    SPI4_CFG2 = (1U << 22)   /* MASTER */
               | (1U << 26);  /* SSM */

    SPI4_IFCR = SPI_IFCR_ALL;              /* clear any flag set during init */
}

/* ------------------------------------------------------------------ */
/* TX                                                                   */
/* ------------------------------------------------------------------ */

void obsw_spi4_write(const uint8_t *buf, uint16_t len)
{
    if (!len) return;

    /* MODF condition is SSI=0 with MASTER+SSM active.  Set SSI=1 first to
     * eliminate the condition, then clear the flag — if you clear IFCR while
     * SSI=0 the hardware immediately re-asserts MODF and SPE can't be set. */
    SPI4_CR1  = SPI_CR1_SSI;
    SPI4_IFCR = SPI_IFCR_ALL;                   /* condition gone — flag clears and stays clear */

    SPI4_CR2 = len;                              /* TSIZE = byte count  */
    SPI4_CR1 |= SPI_CR1_SPE;                    /* enable SPI — MODF cannot fire now */

    /* Load first byte before CSTART so the FIFO is non-empty when the master
     * starts clocking — prevents a mid-frame stall on the STM32H7. */
    SPI4_TXDR8 = buf[0];
    SPI4_CR1 |= SPI_CR1_CSTART;                 /* CSTART(b9) — kick off */

    for (uint16_t i = 1; i < len; i++) {
        while (!(SPI4_SR & SPI_SR_TXP));         /* wait for space       */
        SPI4_TXDR8 = buf[i];
    }
    while (!(SPI4_SR & SPI_SR_EOT));             /* wait for last bit out */

    SPI4_IFCR = SPI_IFCR_ALL;
    /* Setting SPE=0 automatically discards any data left in the RX FIFO
     * (RM0433: "When SPE is cleared, pending data in RXDR is lost").
     * No explicit drain loop needed — and no risk of infinite RXP polling. */
    SPI4_CR1  = SPI_CR1_SSI;                    /* SPE=0, SSI stays 1 */
}

void obsw_spi4_write_byte(uint8_t byte)
{
    obsw_spi4_write(&byte, 1);
}
