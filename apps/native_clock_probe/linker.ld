ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386:i386)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x40000000;
    .text : { *(.text.entry) *(.text*) *(.rodata*) } :text
    ASSERT(SIZEOF(.text) <= 0x20000, "Clock code overlaps fixed record")
    . = 0x40020000;
    .data : { KEEP(*(.data.clock_record)) *(.data*) *(.bss*) *(COMMON) } :data
    ASSERT(native_clock_record == 0x40020000, "Clock record fixed VA")
    ASSERT(SIZEOF(.data) <= 4096, "Clock probe data exceeds one page")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
