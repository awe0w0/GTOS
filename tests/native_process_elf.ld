ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386:i386)
PHDRS { code PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x40010000;
    .text : { *(.text*) *(.rodata*) } :code
    . = 0x40020000;
    .data : { *(.data*) } :data
    .bss : { *(.bss*) *(COMMON) } :data
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
