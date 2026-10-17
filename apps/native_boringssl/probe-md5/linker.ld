ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386:i386)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x40000000;
    .text : { *(.text.entry) *(.text*) *(.rodata*) } :text
    ASSERT(SIZEOF(.text) <= 0x5b000, "crypto code overlaps record")
    . = 0x4005b000;
    .data : { KEEP(*(.data.crypto_record)) *(.data*) *(.bss*) *(COMMON) } :data
    ASSERT(native_crypto_record == 0x4005b000, "crypto record fixed VA")
    ASSERT(SIZEOF(.data) <= 4096, "crypto globals exceed one page")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
