ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386:i386)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x40000000;
    .text : { *(.text.entry) *(.text*) *(.rodata*) } :text
    ASSERT(SIZEOF(.text) <= 0x40000, "string code overlaps record")
    . = 0x40040000;
    .data : { KEEP(*(.data.string_record)) *(.data*) *(.bss*) *(COMMON) } :data
    ASSERT(native_string_record == 0x40040000, "string record fixed VA")
    ASSERT(SIZEOF(.data) <= 4096, "string data exceeds one page")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
