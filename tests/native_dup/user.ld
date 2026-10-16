ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
PHDRS { text PT_LOAD FLAGS(5); record PT_LOAD FLAGS(6); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x40000000;
    .text : { *(.text.entry) *(.text*) *(.rodata*) } :text
    ASSERT(SIZEOF(.text) <= 0x20000, "CPU probe text overlaps record")
    . = 0x40020000;
    .record : { KEEP(*(.data.cpu_record)) } :record
    ASSERT(cpu_record == 0x40020000, "CPU probe record address")
    ASSERT(SIZEOF(.record) <= 4096, "CPU probe record page limit")
    /* Preserve the unmapped page after the record for real copy-bound tests. */
    . = 0x40022000;
    .data : {
        *(.data*) *(.bss*) *(COMMON)
    } :data
    .init_array ALIGN(4) : {
        __init_array_start = .;
        KEEP(*(.init_array*))
        __init_array_end = .;
    } :data
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
