/**
 * @file uart.c
 * @brief MSP430FR5969 UART HAL — implements obsw_io_ops_t.
 *
 * Hardware build: polled UCA0 eUSCI_A (FR5969 register layout).
 * Renode build:   interrupt-driven RX via ring buffer. ISR fires on each
 *                 byte received by the MSP430_USCIA model, draining the
 *                 model's 1-byte FIFO before the next byte arrives.
 */

#include "obsw/hal/io.h"

#include <msp430.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Register addresses                                                  */
/* ------------------------------------------------------------------ */

#ifdef RENODE_BUILD
/* MSP430_USCIA model (F2xxx register layout) mapped at 0x05C0.
 * Use volatile pointer variables — not macros — so the large memory
 * model compiler materialises the address correctly. */
static volatile uint8_t *const _uart_rxbuf = (volatile uint8_t *)0x05C6U;
static volatile uint8_t *const _uart_txbuf = (volatile uint8_t *)0x05C7U;
static volatile uint8_t *const _uart_ifg   = (volatile uint8_t *)0x05DAU;
#define UART_RXBUF (*_uart_rxbuf)
#define UART_TXBUF (*_uart_txbuf)
#define UART_IFG (*_uart_ifg)
#define UART_TXIFG 0x02U
#define UART_RXIFG 0x01U
#else
#define UART_RXBUF UCA0RXBUF
#define UART_TXBUF UCA0TXBUF
#define UART_IFG (*((volatile uint8_t *)(&UCA0IFG)))
#define UART_TXIFG UCTXIFG
#define UART_RXIFG UCRXIFG
#endif

/* ------------------------------------------------------------------ */
/* RX ring buffer — filled by ISR, drained by uart_read()             */
/* ------------------------------------------------------------------ */

#ifdef RENODE_BUILD
#define RX_BUF_LEN 64U
static volatile uint8_t rx_buf[RX_BUF_LEN];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

static inline void rx_push(uint8_t b)
{
    uint16_t next = (uint16_t)((rx_head + 1U) % RX_BUF_LEN);
    if (next != rx_tail) {
        rx_buf[rx_head] = b;
        rx_head         = next;
    }
}

static inline int rx_pop(uint8_t *b)
{
    if (rx_tail == rx_head)
        return 0;
    *b      = rx_buf[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1U) % RX_BUF_LEN);
    return 1;
}

/* UCA0 RX interrupt — drains model's 1-byte FIFO into ring buffer */
void __attribute__((interrupt(USCI_A0_VECTOR))) usci_a0_isr(void)
{
    if (UART_IFG & UART_RXIFG)
        rx_push(UART_RXBUF);
}
#endif

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */

void obsw_uart_init(void)
{
#ifndef RENODE_BUILD
    PM5CTL0 &= ~LOCKLPM5;
    P2SEL1 |= BIT0 | BIT1;
    P2SEL0 &= ~(BIT0 | BIT1);
    UCA0CTLW0 = UCSWRST;
    UCA0CTLW0 |= UCSSEL__SMCLK;
    UCA0BRW   = 104U;
    UCA0MCTLW = 0x0200U;
    UCA0CTLW0 &= ~UCSWRST;
#endif
    /* In RENODE_BUILD the MSP430_USCIA model self-initialises */
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