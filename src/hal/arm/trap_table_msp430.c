/**
 * @file trap_table.c
 * @brief MSP430 trap/exception handler stubs — safe-mode OBC target.
 *
 * The MSP430 has a limited exception model compared to ARM Cortex-A.
 * The primary fault vectors relevant to flight software are:
 *
 *   - Watchdog timer interrupt (WDT_VECTOR)   — hardware watchdog expiry
 *   - Uninitialized interrupt (UNMI_VECTOR)   — NMI from vacant vector
 *   - System NMI (SYSNMI_VECTOR)              — vacant memory access, JTAG
 *
 * On MSP430, unused interrupt vectors must be explicitly handled to
 * avoid runaway execution. The BSL (bootstrap loader) fills vacant
 * vectors with a reset instruction by default — but application code
 * should define all vectors explicitly.
 *
 * The MSP430 is the safe-mode OBC in the dual-OBC topology. In the
 * event of a fault, the correct response is typically:
 *   1. Emit a fault indicator via the inter-OBC link (if available).
 *   2. Reset via watchdog or software reset (PMMSWBOR).
 *
 * Status: stubs — implement per mission before flight.
 */

#include "obsw/fdir/fsm.h"

/* ------------------------------------------------------------------ */
/* Hardware watchdog expiry                                            */
/* ------------------------------------------------------------------ */

/**
 * MSP430 hardware WDT vector.
 * If the software watchdog is not kicked, the hardware WDT fires here.
 * Correct response: log the fault and reset.
 *
 * On MSP430, declare as:
 *   #pragma vector=WDT_VECTOR
 *   __interrupt void obsw_trap_wdt_expiry(void)
 */
__attribute__((weak)) void obsw_trap_wdt_expiry(void)
{
    /*
     * TODO:
     *   Signal fault to primary OBC via inter-OBC link.
     *   PMMSWBOR (software BOR reset) or let WDT reset the device.
     */
    while (1)
        ;
}

/* ------------------------------------------------------------------ */
/* Uninitialized / vacant interrupt vector                            */
/* ------------------------------------------------------------------ */

/**
 * Catches execution reaching a vacant interrupt vector.
 * Should never happen in a correct build — if it does, the vector
 * table is corrupted or an unexpected IRQ is enabled.
 */
__attribute__((weak)) void obsw_trap_vacant_vector(void)
{
    /*
     * TODO:
     *   Signal fault to primary OBC.
     *   Reset.
     */
    while (1)
        ;
}

/* ------------------------------------------------------------------ */
/* System NMI                                                          */
/* ------------------------------------------------------------------ */

/**
 * MSP430 SYSNMI vector — vacant memory access, JTAG mailbox event.
 * On a flight unit with JTAG disabled, this indicates a serious fault.
 *
 * On MSP430, declare as:
 *   #pragma vector=SYSNMI_VECTOR
 *   __interrupt void obsw_trap_system_nmi(void)
 */
__attribute__((weak)) void obsw_trap_system_nmi(void)
{
    /*
     * TODO:
     *   Signal fault to primary OBC.
     *   Reset.
     */
    while (1)
        ;
}