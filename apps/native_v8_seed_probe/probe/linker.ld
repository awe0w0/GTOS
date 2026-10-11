ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386:i386)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x40000000;
    .text : { *(.text.entry) *(.text*) *(.rodata*) } :text
    ASSERT(SIZEOF(.text) <= 0x40000, "Seed consumer code overlaps record")
    . = 0x40040000;
    .data : {
        KEEP(*(.data.file_record)) *(.data*) *(.bss*) *(COMMON)
        . = ALIGN(4);
        __init_array_start = .; KEEP(*(.init_array*)) __init_array_end = .;
        __fini_array_start = .; KEEP(*(.fini_array*)) __fini_array_end = .;
    } :data
    ASSERT(native_file_record == 0x40040000, "Seed record fixed VA")
    ASSERT(SIZEOF(.data) <= 4096, "Seed globals exceed one page")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
