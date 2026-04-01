/* linker.ld — MSP430FR5969 linker script for openobsw
 *
 * Memory map:
 *   SRAM:  0x2000 – 0x27FF  (2 KB)   — stack + .bss + .data
 *   FRAM:  0x4400 – 0xFF7F  (47 KB)  — .text + .rodata + .data init
 *   IVT:   0xFF80 – 0xFFFF  (128 B)  — interrupt vector table
 */

MEMORY
{
    sram  (rwx) : ORIGIN = 0x2000, LENGTH = 0x0800   /* 2 KB  */
    fram  (rx)  : ORIGIN = 0x4400, LENGTH = 0xBB80   /* 47 KB */
    ivt   (rx)  : ORIGIN = 0xFF80, LENGTH = 0x0080   /* 128 B */
}

SECTIONS
{
    /* Code and read-only data in FRAM */
    .text :
    {
        KEEP(*(.vectors))
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(2);
    } > fram

    /* Initialised data: stored in FRAM, copied to SRAM at boot */
    .data :
    {
        _data_start = .;
        *(.data .data.*)
        . = ALIGN(2);
        _data_end = .;
    } > sram AT > fram

    _data_load = LOADADDR(.data);

    /* Zero-initialised data in SRAM */
    .bss :
    {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(2);
        _bss_end = .;
    } > sram

    /* Stack at the top of SRAM — grows downward */
    .stack (NOLOAD) :
    {
        . = ALIGN(2);
        _stack_start = .;
        . += 0x200;     /* 512 bytes — adjust if needed */
        _stack_end = .;
    } > sram

    /* Interrupt vector table */
    .vectors :
    {
        KEEP(*(.vectors))
    } > ivt

    /* Discard C++ exception tables (not used) */
    /DISCARD/ :
    {
        *(.eh_frame)
        *(.ARM.exidx)
    }
}

/* Stack pointer initialised by hardware from reset vector */
PROVIDE(_stack = ORIGIN(sram) + LENGTH(sram));
