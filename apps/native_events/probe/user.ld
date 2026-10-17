ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386:i386)
PHDRS { text PT_LOAD FLAGS(5); record PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x40000000;
    .text : { *(.text*) *(.rodata*) } :text
    . = 0x41000000;
    .record : { KEEP(*(.record)) } :record
    . = ALIGN(4096);
    .data : { *(.data*) *(.bss*) *(COMMON) } :record
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
