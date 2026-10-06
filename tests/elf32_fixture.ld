OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)
PHDRS {
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
    stack PT_GNU_STACK FLAGS(6);
}
SECTIONS {
    . = 0x40000000;
    .text : { *(.text*) } :text
    . = ALIGN(4096);
    .data : { *(.data*) } :data
    .bss (NOLOAD) : { *(.bss*) } :data
    /DISCARD/ : { *(.note*) *(.comment*) *(.eh_frame*) }
}
