ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386:i386)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x40000000;
    .text : { *(.text.entry) *(.text*) *(.rodata*) } :text
    ASSERT(SIZEOF(.text) <= 0x20000, "Heap probe code overlaps fixed record")
    . = 0x40020000;
    .data : { KEEP(*(.data.heap_record)) *(.data*) *(.bss*) *(COMMON) } :data
    ASSERT(native_heap_record == 0x40020000, "Heap record must have fixed user VA")
    ASSERT(SIZEOF(.data) <= 4096, "Heap probe data exceeds one page")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
