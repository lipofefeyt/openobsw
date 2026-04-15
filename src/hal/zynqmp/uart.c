/**
 * @file uart.c
 * @brief ZynqMP PS Cadence UART HAL — implements obsw_io_ops_t.
 *
 * Targets the Cadence UART peripheral on ZynqMP PS:
 *   uart0: base address 0xFF000000
 *   uart1: base address 0xFF010000
 *
 * Polled I/O — no interrupts. Suitable for SIL via Renode and
 * eventually bare-metal ZynqMP PS applications.
 *
 * Register layout: Xilinx UG1085 (Zynq UltraScale+ TRM), Chapter 19.
 *
 * References:
 *   Xilinx PG021 — Cadence UART datasheet
 *   Renode zynqmp.repl — uart0 @ 0xFF000000
 */

#include "obsw/hal/io.h"
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Base address                                                        */
/* ------------------------------------------------------------------ */

#ifndef ZYNQMP_UART_BASE
#define ZYNQMP_UART_BASE 0xFF000000UL   /* uart0 — matches Renode repl */
#endif

/* ------------------------------------------------------------------ */
/* Register offsets (Cadence UART, UG1085 Table 19-3)                 */
/* ------------------------------------------------------------------ */

#define XUARTPS_CR_OFFSET     0x00U  /* Control register              */
#define XUARTPS_MR_OFFSET     0x04U  /* Mode register                 */
#define XUARTPS_IER_OFFSET    0x08U  /* Interrupt enable              */
#define XUARTPS_IDR_OFFSET    0x0CU  /* Interrupt disable             */
#define XUARTPS_IMR_OFFSET    0x10U  /* Interrupt mask                */
#define XUARTPS_ISR_OFFSET    0x14U  /* Interrupt status              */
#define XUARTPS_BAUDGEN_OFFSET 0x18U /* Baud rate generator          */
#define XUARTPS_RXTOUT_OFFSET 0x1CU  /* RX timeout                   */
#define XUARTPS_RXWM_OFFSET   0x20U  /* RX FIFO trigger level        */
#define XUARTPS_MODEMCR_OFFSET 0x24U /* Modem control                */
#define XUARTPS_MODEMSR_OFFSET 0x28U /* Modem status                 */
#define XUARTPS_SR_OFFSET     0x2CU  /* Channel status register       */
#define XUARTPS_FIFO_OFFSET   0x30U  /* TX/RX FIFO                   */
#define XUARTPS_BAUDDIV_OFFSET 0x34U /* Baud rate divider            */

/* Control register bits */
#define XUARTPS_CR_TX_EN      0x00000010U  /* TX enable                */
#define XUARTPS_CR_RX_EN      0x00000004U  /* RX enable                */
#define XUARTPS_CR_TXRST      0x00000002U  /* TX logic reset           */
#define XUARTPS_CR_RXRST      0x00000001U  /* RX logic reset           */

/* Status register bits */
#define XUARTPS_SR_TXFULL     0x00000010U  /* TX FIFO full             */
#define XUARTPS_SR_TXEMPTY    0x00000008U  /* TX FIFO empty            */
#define XUARTPS_SR_RXEMPTY    0x00000002U  /* RX FIFO empty            */
#define XUARTPS_SR_RXOVR      0x00000001U  /* RX overflow              */

/* ------------------------------------------------------------------ */
/* Register accessors                                                  */
/* ------------------------------------------------------------------ */

static inline uint32_t uart_read_reg(uint32_t offset)
{
    return *(volatile uint32_t *)(ZYNQMP_UART_BASE + offset);
}

static inline void uart_write_reg(uint32_t offset, uint32_t val)
{
    *(volatile uint32_t *)(ZYNQMP_UART_BASE + offset) = val;
}

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */

void obsw_uart_init(void)
{
    /* Reset TX and RX FIFOs */
    uart_write_reg(XUARTPS_CR_OFFSET,
                   XUARTPS_CR_TXRST | XUARTPS_CR_RXRST);

    /* Enable TX and RX */
    uart_write_reg(XUARTPS_CR_OFFSET,
                   XUARTPS_CR_TX_EN | XUARTPS_CR_RX_EN);

    /*
     * Baud rate: Renode Cadence_UART model accepts any rate.
     * For real hardware at 100MHz ref clock, 115200 baud:
     *   BAUDDIV = 6, BAUDGEN = 62  → actual = 100e6 / (62+1) / (6+1) ≈ 115207
     */
#ifdef ZYNQMP_UART_BAUD_INIT
    uart_write_reg(XUARTPS_BAUDDIV_OFFSET, 6U);
    uart_write_reg(XUARTPS_BAUDGEN_OFFSET, 62U);
#endif
}

/* ------------------------------------------------------------------ */
/* I/O operations                                                      */
/* ------------------------------------------------------------------ */

static int uart_write_fn(const uint8_t *buf, uint16_t len, void *ctx)
{
    (void)ctx;
    for (uint16_t i = 0; i < len; i++) {
        /* Wait until TX FIFO has space */
        while (uart_read_reg(XUARTPS_SR_OFFSET) & XUARTPS_SR_TXFULL)
            ;
        uart_write_reg(XUARTPS_FIFO_OFFSET, (uint32_t)buf[i]);
    }
    return (int)len;
}

static int uart_read_fn(uint8_t *buf, uint16_t len, void *ctx)
{
    (void)ctx;
    uint16_t n = 0;
    while (n < len) {
        if (uart_read_reg(XUARTPS_SR_OFFSET) & XUARTPS_SR_RXEMPTY)
            break;
        buf[n++] = (uint8_t)(uart_read_reg(XUARTPS_FIFO_OFFSET) & 0xFFU);
    }
    return (int)n;
}

obsw_io_ops_t obsw_uart_ops = {
    .write = uart_write_fn,
    .read  = uart_read_fn,
    .ctx   = (void *)0,
};
