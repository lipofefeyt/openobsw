/* zynqmp.ld — linker script for ZynqMP APU bare-metal
 * Single DDR region so objcopy -O binary produces a compact flat image.
 * Loads at 0x00400000 (matches Renode ddrLowApu).
 * Stack pointer is set to 0x0FFFFFFF in startup.S (top of 256 MB DDR).
 */
ENTRY(_start)

MEMORY {
    DDR (rwx) : ORIGIN = 0x00400000, LENGTH = 64M
}

SECTIONS {
    .text : {
        *(.text.boot)
        *(.text*)
        *(.rodata*)
    } > DDR

    .data : {
        *(.data*)
    } > DDR

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } > DDR
}
