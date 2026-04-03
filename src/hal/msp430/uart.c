/**
 * @file uart.c
 * @brief MSP430FR5969 UART HAL — implements obsw_io_ops_t.
 *
 * Uses UCA0 on P2.0 (TXD) / P2.1 (RXD) — the backchannel UART
 * connected to the eZ-FET USB on the FR5969 LaunchPad (COM port).
 *
 * Reference: MSP-EXP430FR5969 User Guide SLAU535B §2.2.4
 *
 * Baud: 9600 at 1 MHz DCO (MSP430FR5969 power-on default).
 * Values from SLAU367 Table 22-4: UCBRx=104, UCBRSx=1, UCOS16=0.
 */

/* obsw headers first to avoid type conflicts with msp430 headers */
#include "obsw/hal/io.h"
#include <msp430.h>
#include <stdint.h>

void obsw_uart_init(void)
{
    /* Unlock GPIO high-impedance mode — mandatory on FR5969 at startup */
    PM5CTL0 &= ~LOCKLPM5;

    /* Configure P2.0 = UCA0TXD, P2.1 = UCA0RXD */
    P2SEL1 |=  BIT0 | BIT1;
    P2SEL0 &= ~(BIT0 | BIT1);

    /* Configure UCA0 for UART */
    UCA0CTLW0  = UCSWRST;           /* hold in reset during config   */
    UCA0CTLW0 |= UCSSEL__SMCLK;     /* clock source: SMCLK (1 MHz)   */

    /* 9600 baud @ 1 MHz: UCBRx=104, UCBRSx=0x02, no oversampling */
    UCA0BRW   = 104U;
    UCA0MCTLW = 0x0200U;            /* UCBRSx=0x02, UCOS16=0         */

    UCA0CTLW0 &= ~UCSWRST;         /* release from reset             */
}

static int uart_write(const uint8_t *buf, uint16_t len, void *ctx)
{
    (void)ctx;
    for (uint16_t i = 0; i < len; i++) {
        while (!(UCA0IFG & UCTXIFG));
        UCA0TXBUF = buf[i];
    }
    return (int)len;
}

static int uart_read(uint8_t *buf, uint16_t len, void *ctx)
{
    (void)ctx;
    uint16_t n = 0;
    while (n < len) {
        if (UCA0IFG & UCRXIFG)
            buf[n++] = UCA0RXBUF;
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