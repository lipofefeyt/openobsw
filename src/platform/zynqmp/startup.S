/* startup.S — minimal AArch64 bare-metal startup for ZynqMP APU */
.section .text.boot
.global _start

_start:
    /* Set up stack pointer — use top of first 256MB DDR */
    ldr x0, =0x0FFFFFFF
    mov sp, x0

    /* Clear BSS */
    ldr x0, =__bss_start
    ldr x1, =__bss_end
    mov x2, #0
1:  cmp x0, x1
    bge 2f
    str x2, [x0], #8
    b   1b
2:
    /* Jump to C main */
    bl main

    /* Hang if main returns */
hang:
    b hang
