/* zynqmp.ld — linker script for ZynqMP APU bare-metal
 * Separates RX (text) and RW (data) segments to avoid RWX warning.
 * Loads into DDR at 0x00400000 (matches Renode ddrLowApu).
 */
ENTRY(_start)

MEMORY {
    DDR_RX  (rx)  : ORIGIN = 0x00400000, LENGTH = 128M
    DDR_RW  (rw)  : ORIGIN = 0x08000000, LENGTH = 128M
}

SECTIONS {
    .text : {
        *(.text.boot)
        *(.text*)
        *(.rodata*)
    } > DDR_RX

    .data : {
        *(.data*)
    } > DDR_RW

    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } > DDR_RW

    .stack (NOLOAD) : {
        . = ALIGN(16);
        . += 0x10000;
    } > DDR_RW
}
