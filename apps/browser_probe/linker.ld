OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)
PHDRS {
    code PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
    stack PT_GNU_STACK FLAGS(6);
}
SECTIONS {
    . = 0x40000000;
    .text : { *(.text .text.*) *(.rodata .rodata.*) } :code
    ASSERT(SIZEOF(.text) <= 4096, "probe text+rodata exceeds one page")
    . = 0x40002000;
    .data : { *(.data .data.*) } :data
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) } :data
    ASSERT(SIZEOF(.data) + SIZEOF(.bss) <= 4096, "probe data+bss exceeds one page")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
