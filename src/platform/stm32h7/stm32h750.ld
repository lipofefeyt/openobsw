/* src/platform/stm32h7/stm32h750.ld
 *
 * STM32H750VBT6 — internal flash layout
 *   Flash: 128KB at 0x08000000 (vector table + code)
 *   RAM:   1MB  at 0x20000000 (DTCM 128KB + AXI SRAM 512KB + SRAM1/2/3)
 *
 * For larger binaries, use QSPI XIP (0x90000000) with WeAct bootloader.
 * This linker script targets internal flash only — keeps it simple.
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 128K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 512K   /* AXI SRAM */
    DTCM  (rwx) : ORIGIN = 0x20000000, LENGTH = 128K   /* DTCM — fastest */
}

ENTRY(Reset_Handler)

SECTIONS
{
    /* Vector table + code in flash */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* Initialised data: stored in flash, copied to RAM at startup */
    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    /* Zero-initialised data */
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* Stack at top of RAM */
    ._stack :
    {
        . = ALIGN(8);
        . = . + 0x2000;   /* 8KB stack */
        . = ALIGN(8);
        _estack = .;
    } > RAM

    /DISCARD/ :
    {
        libc.a ( * )
        libm.a ( * )
        libgcc.a ( * )
    }
}