ENTRY(NativeClassStart)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386:i386)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x40000000;
    .text : { *(.text.entry) *(.text*) *(.rodata*) } :text
    . = ALIGN(4096);
    .data : {
        KEEP(*(.data.class_record))
        *(.data*)
        . = ALIGN(4);
        __init_array_start = .; KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .;
        __fini_array_start = .; KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*))) KEEP(*(.fini_array)) __fini_array_end = .;
        *(.bss*) *(COMMON)
    } :data
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
