/**
 * @file src/hal/stm32h7/uart.c
 * @brief STM32H750 USART3 HAL (PD8 TX / PD9 RX).
 *
 * WeAct STM32H750 board exposes USART3 on the USB-C debug pins.
 * Baud rate: 115200, 8N1, no flow control.
 * Clock: assumes SYSCLK = 480MHz, APB1 = 120MHz → BRR = 120000000/115200.
 *
 * Register-level only — no HAL library dependency.
 *
 * Implements: obsw_io_ops_t for use as obsw_uart_ops.
 */

#include "obsw/hal/io.h"
#include <stddef.h>

#include <stdint.h>
 
/* ------------------------------------------------------------------ */
/* STM32H7 peripheral base addresses                                   */
/* ------------------------------------------------------------------ */
 
#define RCC_BASE        0x58024400UL
#define GPIOD_BASE      0x58020C00UL
#define USART3_BASE     0x40004800UL
 
/* RCC */
#define RCC_AHB4ENR     (*(volatile uint32_t *)(RCC_BASE + 0x0E0))
#define RCC_APB1LENR    (*(volatile uint32_t *)(RCC_BASE + 0x0E8))
 
/* GPIOD */
#define GPIOD_MODER     (*(volatile uint32_t *)(GPIOD_BASE + 0x00))
#define GPIOD_OSPEEDR   (*(volatile uint32_t *)(GPIOD_BASE + 0x08))
#define GPIOD_AFRH      (*(volatile uint32_t *)(GPIOD_BASE + 0x24))
 
/* USART3 */
#define USART3_CR1      (*(volatile uint32_t *)(USART3_BASE + 0x00))
#define USART3_CR2      (*(volatile uint32_t *)(USART3_BASE + 0x04))
#define USART3_CR3      (*(volatile uint32_t *)(USART3_BASE + 0x08))
#define USART3_BRR      (*(volatile uint32_t *)(USART3_BASE + 0x0C))
#define USART3_ISR      (*(volatile uint32_t *)(USART3_BASE + 0x1C))
#define USART3_RDR      (*(volatile uint32_t *)(USART3_BASE + 0x24))
#define USART3_TDR      (*(volatile uint32_t *)(USART3_BASE + 0x28))
 
/* ISR bits */
#define USART_ISR_TXE   (1U << 7)   /* TX data register empty */
#define USART_ISR_TC    (1U << 6)   /* TX complete            */
#define USART_ISR_RXNE  (1U << 5)   /* RX not empty           */
 
/* CR1 bits */
#define USART_CR1_UE    (1U << 0)
#define USART_CR1_RE    (1U << 2)
#define USART_CR1_TE    (1U << 3)
#define USART_CR1_OVER8 (1U << 15)
 
/* ------------------------------------------------------------------ */
/* Initialisation                                                       */
/* ------------------------------------------------------------------ */
 
void obsw_uart_init(void)
{
    /* 1. Enable clocks: GPIOD (AHB4), USART3 (APB1L) */
    RCC_AHB4ENR  |= (1U << 3);   /* GPIODEN */
    RCC_APB1LENR |= (1U << 18);  /* USART3EN */
 
    /* 2. Configure PD8 (TX) and PD9 (RX) as AF7 (USART3) */
    /* MODER: 10 = alternate function for bits [17:16] (PD8) and [19:18] (PD9) */
    GPIOD_MODER &= ~((3U << 16) | (3U << 18));
    GPIOD_MODER |=  ((2U << 16) | (2U << 18));
 
    /* OSPEEDR: high speed */
    GPIOD_OSPEEDR |= (3U << 16) | (3U << 18);
 
    /* AFRH: AF7 for PD8 (bits [3:0]) and PD9 (bits [7:4]) */
    GPIOD_AFRH &= ~(0xFFU);
    GPIOD_AFRH |=  (7U << 0) | (7U << 4);
 
    /* 3. Configure USART3: 115200 baud, 8N1 */
    USART3_CR1 = 0;                        /* Disable while configuring */
    USART3_CR2 = 0;
    USART3_CR3 = 0;
 
    /* BRR = APB1_CLK / baud = 120000000 / 115200 = 1041 */
    USART3_BRR = 555U;
 
    /* Enable TX, RX, USART */
    USART3_CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}
 
/* ------------------------------------------------------------------ */
/* obsw_io_ops_t implementation                                         */
/* ------------------------------------------------------------------ */
 
static int uart_write(const uint8_t *buf, uint16_t len, void *ctx)
{
    (void)ctx;
    for (uint16_t i = 0; i < len; i++) {
        while (!(USART3_ISR & USART_ISR_TXE))
            ;
        USART3_TDR = buf[i];
    }
    /* Wait for TX complete */
    while (!(USART3_ISR & USART_ISR_TC))
        ;
    return (int)len;
}
 
static int uart_read(uint8_t *buf, uint16_t len, void *ctx)
{
    (void)ctx;
    for (uint16_t i = 0; i < len; i++) {
        while (!(USART3_ISR & USART_ISR_RXNE))
            ;
        buf[i] = (uint8_t)(USART3_RDR & 0xFFU);
    }
    return (int)len;
}
 
obsw_io_ops_t obsw_uart_ops = {
    .write = uart_write,
    .read  = uart_read,
    .ctx   = NULL,
};