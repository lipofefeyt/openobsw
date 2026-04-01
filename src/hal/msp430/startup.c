/**
 * @file startup.c
 * @brief MSP430FR5969 startup — clock init and .data/.bss init.
 *
 * Configures the system clock to 8 MHz MCLK/SMCLK from DCO,
 * copies .data from FRAM to SRAM, zeroes .bss, then calls main().
 *
 * This replaces the CRT startup provided by the libc runtime to keep
 * the binary self-contained.
 */

#include <msp430.h>
#include <stdint.h>
#include <string.h>

/* Symbols defined by linker.ld */
extern uint8_t _data_start;
extern uint8_t _data_end;
extern uint8_t _data_load;
extern uint8_t _bss_start;
extern uint8_t _bss_end;

extern int main(void);

/* ------------------------------------------------------------------ */
/* Clock configuration — 8 MHz DCO                                    */
/* ------------------------------------------------------------------ */

static void clock_init(void)
{
    /* Disable watchdog while configuring */
    WDTCTL = WDTPW | WDTHOLD;

    /* Configure DCO at 8 MHz */
    CSCTL0_H = CSKEY >> 8;                  /* unlock CS registers */
    CSCTL1   = DCOFSEL_6;                   /* DCO = 8 MHz         */
    CSCTL2   = SELA__VLOCLK                 /* ACLK  = VLO         */
               | SELS__DCOCLK               /* SMCLK = DCO         */
               | SELM__DCOCLK;              /* MCLK  = DCO         */
    CSCTL3   = DIVA__1 | DIVS__1 | DIVM__1; /* all dividers = 1 */
    CSCTL0_H = 0;                           /* re-lock CS registers */
}

/* ------------------------------------------------------------------ */
/* Reset handler                                                       */
/* ------------------------------------------------------------------ */

void __attribute__((naked, section(".vectors"), used)) reset_handler(void)
{
    /* Initialise stack pointer — the linker places _stack at top of SRAM */
    __asm__ volatile("mov #_stack, r1\n" ::: "memory");

    clock_init();

    /* Copy initialised data from FRAM load address to SRAM */
    size_t data_size = (size_t)(&_data_end - &_data_start);
    if (data_size > 0)
        memcpy(&_data_start, &_data_load, data_size);

    /* Zero BSS */
    size_t bss_size = (size_t)(&_bss_end - &_bss_start);
    if (bss_size > 0)
        memset(&_bss_start, 0, bss_size);

    /* Call application */
    main();

    /* Should never return — halt */
    while (1)
        ;
}