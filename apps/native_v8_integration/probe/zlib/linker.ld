ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386:i386)
PHDRS { entry PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); body PT_LOAD FLAGS(5); }
SECTIONS {
    . = 0x40000000;
    .entry : { *(.text.entry) } :entry
    ASSERT(SIZEOF(.entry) <= 4096, "Zlib entry exceeds one page")
    . = 0x40040000;
    .data : {
        KEEP(*(.data.file_record)) *(.data*) *(.bss*) *(COMMON)
        . = ALIGN(4);
        __init_array_start = .; KEEP(*(.init_array*)) __init_array_end = .;
        __fini_array_start = .; KEEP(*(.fini_array*)) __fini_array_end = .;
    } :data
    ASSERT(native_file_record == 0x40040000, "Zlib record fixed VA")
    ASSERT(SIZEOF(.data) <= 4096, "Zlib globals exceed one page")
    . = 0x40041000;
    .body : { *(.text*) *(.rodata*) } :body
    ASSERT(SIZEOF(.body) <= 0xfe000, "Zlib image exceeds unchanged 256-page allowance")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
