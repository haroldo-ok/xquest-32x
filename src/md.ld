/* Minimal linker script for the 68000 boot blobs (m68k_crt0/crt1).
 * They are position-fixed code assembled to raw binaries at origin 0. */
OUTPUT_ARCH(m68k)
MEMORY
{
    rom (rx) : ORIGIN = 0x00000000, LENGTH = 0x00400000
}
SECTIONS
{
    .text 0x00000000 :
    {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        *(.data)
    } > rom
}
