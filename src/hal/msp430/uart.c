/**
 * @file uart.c
 * @brief MSP430FR5969 UART HAL — implements obsw_io_ops_t.
 *
 * Uses USCI_A1 on P2.0 (TXD) / P2.1 (RXD) — the UART connected to
 * the backchannel USB on the MSP430FR5969 LaunchPad.
 *
 * Baud rate: 9600 at 8 MHz SMCLK (configurable via OBSW_UART_BAUD).
 *
 * TC reception: blocking byte-by-byte reads in obsw_io_read().
 * TM transmission: blocking byte-by-byte writes in obsw_io_write().
 *
 * For Renode emulation: the same peripheral is modelled as a UART
 * connected to the sentinel peripheral. obsw_io_write() doubles as
 * the lockstep sync channel.
 */

/* obsw headers must come before msp430.h to avoid type conflicts */
#include "obsw/hal/io.h"

#include <msp430.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

#ifndef OBSW_UART_BAUD
#define OBSW_UART_BAUD 9600UL
#endif

#ifndef OBSW_SMCLK_HZ
#define OBSW_SMCLK_HZ 8000000UL
#endif

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */

void obsw_uart_init(void)
{
    /* Configure P2.0 TXD, P2.1 RXD for USCI_A1 */
    P2SEL1 |= BIT0 | BIT1;
    P2SEL0 &= ~(BIT0 | BIT1);

    /* Put USCI_A1 in reset during configuration */
    UCA1CTLW0 = UCSWRST;

    /* SMCLK source, 8N1 */
    UCA1CTLW0 |= UCSSEL__SMCLK;

    /* Baud rate registers (see SLAU367 Table 30-5) */
    uint32_t n = OBSW_SMCLK_HZ / OBSW_UART_BAUD;
    UCA1BRW    = (uint16_t)(n >> 4); /* integer part >> 4 */
    UCA1MCTLW  = (uint16_t)(((n & 0xFU) << 4) | UCOS16);

    /* Release from reset */
    UCA1CTLW0 &= ~UCSWRST;
}

/* ------------------------------------------------------------------ */
/* obsw_io_ops_t implementation                                        */
/* ------------------------------------------------------------------ */

static int uart_write(const uint8_t *buf, uint16_t len, void *ctx)
{
    (void)ctx;
    for (uint16_t i = 0; i < len; i++) {
        while (!(UCA1IFG & UCTXIFG))
            ; /* wait for TX buffer empty */
        UCA1TXBUF = buf[i];
    }
    return (int)len;
}

static int uart_read(uint8_t *buf, uint16_t len, void *ctx)
{
    (void)ctx;
    uint16_t n = 0;
    while (n < len) {
        if (UCA1IFG & UCRXIFG) {
            buf[n++] = UCA1RXBUF;
        } else {
            break; /* non-blocking: return what's available */
        }
    }
    return (int)n;
}

/* Singleton I/O ops — pass to obsw_tc_dispatcher via obsw_io_ops_t */
obsw_io_ops_t obsw_uart_ops = {
    .write = uart_write,
    .read  = uart_read,
    .ctx   = NULL,
};