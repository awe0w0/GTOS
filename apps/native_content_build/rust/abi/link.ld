/* One image at a fixed load address, no interpreter, no dynamic sections: the same shape the browser probe uses, so the ABI object
   is inspected the way the real image would be. */
ENTRY(_start)
SECTIONS
{
    . = 0x40000000;
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.* COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
