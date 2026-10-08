ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386:i386)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x40000000;
    .text : { *(.text.entry) *(.text*) *(.rodata*) } :text
    ASSERT(SIZEOF(.text) <= 0x20000, "Process info code overlaps fixed record")
    . = 0x40020000;
    .data : { KEEP(*(.data.process_info_record)) *(.data*) *(.bss*) *(COMMON) } :data
    ASSERT(native_process_info_record == 0x40020000, "Process info record fixed VA")
    ASSERT(SIZEOF(.data) <= 4096, "Process info probe data exceeds one page")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
