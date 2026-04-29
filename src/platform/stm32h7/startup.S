/* src/platform/stm32h7/startup.S
 *
 * Cortex-M7 minimal startup for STM32H750VBT6.
 * Sets up the vector table, copies .data from flash to RAM,
 * zeroes .bss, then calls main().
 */

    .syntax unified
    .cpu cortex-m7
    .thumb

/* ------------------------------------------------------------------ */
/* Stack and heap                                                       */
/* ------------------------------------------------------------------ */
    .section .stack, "aw", %nobits
    .align 3
    .globl __stack_start__
__stack_start__:
    .space 0x2000          /* 8KB stack */
    .globl __stack_end__
__stack_end__:

/* ------------------------------------------------------------------ */
/* Vector table                                                         */
/* ------------------------------------------------------------------ */
    .section .isr_vector, "a", %alloc
    .align 2
    .globl g_pfnVectors
g_pfnVectors:
    .word __stack_end__            /* Initial stack pointer */
    .word Reset_Handler            /* Reset                 */
    .word NMI_Handler              /* NMI                   */
    .word HardFault_Handler        /* Hard Fault            */
    .word MemManage_Handler        /* MPU Fault             */
    .word BusFault_Handler         /* Bus Fault             */
    .word UsageFault_Handler       /* Usage Fault           */
    .word 0                        /* Reserved              */
    .word 0                        /* Reserved              */
    .word 0                        /* Reserved              */
    .word 0                        /* Reserved              */
    .word SVC_Handler              /* SVCall                */
    .word DebugMon_Handler         /* Debug Monitor         */
    .word 0                        /* Reserved              */
    .word PendSV_Handler           /* PendSV                */
    .word SysTick_Handler          /* SysTick               */

    /* External interrupts — fill as needed */
    .rept 150
    .word Default_Handler
    .endr

/* ------------------------------------------------------------------ */
/* Reset handler                                                        */
/* ------------------------------------------------------------------ */
    .section .text.Reset_Handler
    .weak Reset_Handler
    .type Reset_Handler, %function
Reset_Handler:
    /* Copy .data from flash to RAM */
    ldr  r0, =_sidata
    ldr  r1, =_sdata
    ldr  r2, =_edata
    b    .Lcopy_check
.Lcopy_loop:
    ldr  r3, [r0], #4
    str  r3, [r1], #4
.Lcopy_check:
    cmp  r1, r2
    bcc  .Lcopy_loop

    /* Zero .bss */
    ldr  r0, =_sbss
    ldr  r1, =_ebss
    mov  r2, #0
    b    .Lzero_check
.Lzero_loop:
    str  r2, [r0], #4
.Lzero_check:
    cmp  r0, r1
    bcc  .Lzero_loop

    /* Call main */
    bl   main
    b    .

/* ------------------------------------------------------------------ */
/* Default / weak handlers                                              */
/* ------------------------------------------------------------------ */
    .section .text
    .weak NMI_Handler
    .type NMI_Handler, %function
NMI_Handler:
    b .

    .weak HardFault_Handler
    .type HardFault_Handler, %function
HardFault_Handler:
    b .

    .weak MemManage_Handler
MemManage_Handler:    b .
    .weak BusFault_Handler
BusFault_Handler:     b .
    .weak UsageFault_Handler
UsageFault_Handler:   b .
    .weak SVC_Handler
SVC_Handler:          b .
    .weak DebugMon_Handler
DebugMon_Handler:     b .
    .weak PendSV_Handler
PendSV_Handler:       b .
    .weak SysTick_Handler
SysTick_Handler:      b .

    .weak Default_Handler
    .type Default_Handler, %function
Default_Handler:
    b .