ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386:i386)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x40000000;
    .text : { *(.text.entry) *(.text*) *(.rodata*) } :text
    ASSERT(SIZEOF(.text) <= 0x20000, "Realtime code overlaps record")
    . = 0x40020000;
    .data : { KEEP(*(.data.realtime_record)) *(.data*) *(.bss*) *(COMMON) } :data
    ASSERT(native_realtime_record == 0x40020000, "Realtime record fixed VA")
    ASSERT(SIZEOF(.data) <= 4096, "Realtime data exceeds one page")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
