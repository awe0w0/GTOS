ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386:i386)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x40000000;
    .text : { *(.text.entry) *(.text*) *(.rodata*) } :text
    ASSERT(SIZEOF(.text) <= 4096, "Native probe code exceeds one page")
    . = 0x40002000;
    .data : { *(.data*) *(.bss*) *(COMMON) } :data
    ASSERT(SIZEOF(.data) <= 4096, "Native probe data exceeds one page")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
