ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386:i386)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x40000000;
    .text : { *(.text.entry) *(.text*) *(.rodata*) } :text
    ASSERT(SIZEOF(.text) <= 0x100000, "Chrono code overlaps record")
    . = 0x40100000;
    .data : { KEEP(*(.data.chrono_record)) *(.data*) *(.bss*) *(COMMON) } :data
    ASSERT(native_chrono_record == 0x40100000, "Chrono record fixed VA")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
