/**
 * @file trap_table.c
 * @brief ARM Cortex-A trap/exception handler stubs — ZynqMP target.
 *
 * These are weak-linked stubs. The application overrides them by
 * providing strong definitions. Each handler must:
 *   1. Emit an S5 HIGH event (if S5 context is available).
 *   2. Call obsw_fsm_to_safe() to enter safe mode.
 *   3. Either halt (flight) or reset (depending on mission policy).
 *
 * On ZynqMP (Cortex-A53), the exception vector table is configured
 * by the BSP (Xilinx standalone / FreeRTOS port). These handlers
 * plug into the BSP exception registration API:
 *   Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_xxx, handler, data)
 *
 * DO NOT implement FDIR logic in assembly stubs — register a C handler
 * via the BSP and call these functions from it.
 *
 * Status: stubs — implement per mission before flight.
 */

#include "obsw/fdir/fsm.h"

/* ------------------------------------------------------------------ */
/* Undefined instruction                                               */
/* ------------------------------------------------------------------ */

/**
 * Called when the CPU encounters an undefined instruction.
 * Likely causes: corrupted .text, misaligned branch, missing FPU.
 */
__attribute__((weak)) void obsw_trap_undefined_instruction(void *data)
{
    (void)data;
    /*
     * TODO:
     *   obsw_s5_report(&s5_ctx, OBSW_S5_HIGH, EVENT_TRAP_UNDEF, NULL, 0);
     *   obsw_fsm_to_safe(&fsm_ctx);
     *   while(1);   // halt — or trigger hardware watchdog reset
     */
    while (1)
        ;
}

/* ------------------------------------------------------------------ */
/* Data abort                                                          */
/* ------------------------------------------------------------------ */

/**
 * Called on a data abort (bad memory access — null pointer, MMU fault).
 * Most common fault class in flight software.
 */
__attribute__((weak)) void obsw_trap_data_abort(void *data)
{
    (void)data;
    /*
     * TODO:
     *   obsw_s5_report(&s5_ctx, OBSW_S5_HIGH, EVENT_TRAP_DATA_ABORT, NULL, 0);
     *   obsw_fsm_to_safe(&fsm_ctx);
     *   while(1);
     */
    while (1)
        ;
}

/* ------------------------------------------------------------------ */
/* Prefetch abort                                                      */
/* ------------------------------------------------------------------ */

/**
 * Called on a prefetch abort (instruction fetch from invalid address).
 * Often indicates stack overflow or corrupted PC.
 */
__attribute__((weak)) void obsw_trap_prefetch_abort(void *data)
{
    (void)data;
    /*
     * TODO:
     *   obsw_s5_report(&s5_ctx, OBSW_S5_HIGH, EVENT_TRAP_PREFETCH, NULL, 0);
     *   obsw_fsm_to_safe(&fsm_ctx);
     *   while(1);
     */
    while (1)
        ;
}

/* ------------------------------------------------------------------ */
/* Stack overflow sentinel                                             */
/* ------------------------------------------------------------------ */

/**
 * Called when a stack canary / guard region is breached.
 * On ZynqMP with FreeRTOS: implement vApplicationStackOverflowHook().
 * On bare metal: place a known pattern at stack base and check each cycle.
 */
__attribute__((weak)) void obsw_trap_stack_overflow(void *data)
{
    (void)data;
    /*
     * TODO:
     *   obsw_s5_report(&s5_ctx, OBSW_S5_HIGH, EVENT_TRAP_STACK_OVF, NULL, 0);
     *   obsw_fsm_to_safe(&fsm_ctx);
     *   while(1);
     */
    while (1)
        ;
}