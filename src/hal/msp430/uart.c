/**
 * @file uart.c
 * @brief MSP430FR5969 UART HAL — implements obsw_io_ops_t.
 *
 * Two register layouts:
 *
 *   Default (RENODE_BUILD not set):
 *     FR5969 eUSCI_A at 0x05C0 — RXBUF=0x05CC, TXBUF=0x05CE, IFG=0x05DA
 *
 *   RENODE_BUILD:
 *     Renode's MSP430_USCIA model (F2xxx layout) — RXBUF=0x05C6, TXBUF=0x05C7
 *     IFG exposed at 0x05DA via BusMultiRegistration in msp430fr5969.repl
 *
 * Baud: 9600 at 1 MHz DCO (MSP430FR5969 power-on default).
 */

#include "obsw/hal/io.h"

#include <msp430.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Register addresses                                                  */
/* ------------------------------------------------------------------ */

#ifdef RENODE_BUILD
/* Renode MSP430_USCIA model — F2xxx USCI register layout (8-bit)    */
#define UART_RXBUF (*((volatile uint8_t *)0x05C6U))
#define UART_TXBUF (*((volatile uint8_t *)0x05C7U))
#define UART_IFG (*((volatile uint8_t *)0x05DAU))
#define UART_TXIFG 0x02U
#define UART_RXIFG 0x01U
#else
/* FR5969 eUSCI_A — real hardware register layout (16-bit peripherals) */
#define UART_RXBUF UCA0RXBUF
#define UART_TXBUF UCA0TXBUF
#define UART_IFG (*((volatile uint8_t *)(&UCA0IFG)))
#define UART_TXIFG UCTXIFG
#define UART_RXIFG UCRXIFG
#endif

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */

void obsw_uart_init(void)
{
#ifndef RENODE_BUILD
    /* Unlock GPIO high-impedance mode — mandatory on FR5969 at startup */
    PM5CTL0 &= ~LOCKLPM5;

    /* Configure P2.0 = UCA0TXD, P2.1 = UCA0RXD */
    P2SEL1 |= BIT0 | BIT1;
    P2SEL0 &= ~(BIT0 | BIT1);

    /* Configure UCA0 for UART, 9600 baud @ 1MHz */
    UCA0CTLW0 = UCSWRST;
    UCA0CTLW0 |= UCSSEL__SMCLK;
    UCA0BRW   = 104U;
    UCA0MCTLW = 0x0200U;
    UCA0CTLW0 &= ~UCSWRST;
#endif
    /* In RENODE_BUILD, Renode's USCIA model initialises itself */
}

/* ------------------------------------------------------------------ */
/* I/O operations                                                      */
/* ------------------------------------------------------------------ */

static int uart_write(const uint8_t *buf, uint16_t len, void *ctx)
{
    (void)ctx;
    for (uint16_t i = 0; i < len; i++) {
        while (!(UART_IFG & UART_TXIFG))
            ;
        UART_TXBUF = buf[i];
    }
    return (int)len;
}

static int uart_read(uint8_t *buf, uint16_t len, void *ctx)
{
    (void)ctx;
    uint16_t n = 0;
    while (n < len) {
        if (UART_IFG & UART_RXIFG)
            buf[n++] = UART_RXBUF;
        else
            break;
    }
    return (int)n;
}

obsw_io_ops_t obsw_uart_ops = {
    .write = uart_write,
    .read  = uart_read,
    .ctx   = (void *)0,
};