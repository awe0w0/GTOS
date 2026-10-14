ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386:i386)
PHDRS { text PT_LOAD FLAGS(5); record PT_LOAD FLAGS(6); pages PT_LOAD FLAGS(6); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x40000000;
    .text : { *(.text.entry) *(.text*) *(.rodata*) } :text
    ASSERT(SIZEOF(.text) <= 0x20000, "readonly text overlaps record")
    . = 0x40020000;
    .record : { KEEP(*(.data.readonly_record)) } :record
    ASSERT(native_readonly_record == 0x40020000, "readonly record fixed VA")
    ASSERT(SIZEOF(.record) <= 4096, "readonly record exceeds page")
    . = 0x40030000;
    .pages : { KEEP(*(.data.readonly_pages)) } :pages
    ASSERT(SIZEOF(.pages) == 8192, "two exact test pages required")
    . = 0x40040000;
    .data : {
        *(.data*) *(.bss*) *(COMMON)
        . = ALIGN(4);
        __init_array_start = .;
        KEEP(*(.init_array .init_array.*))
        __init_array_end = .;
    } :data
    ASSERT(SIZEOF(.data) <= 0x40000, "readonly V8 globals exceed budget")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
