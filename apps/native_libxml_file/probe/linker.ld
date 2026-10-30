ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386:i386)
/*
 * The user image for the libxml2 parse link. It is deliberately *not* the C FILE probe script: that one pins a FILE record page at
 * 0x40040000 and restricts globals to one page, which is right for a syscall suite and wrong for a library with a few hundred kilobytes
 * of data. What it keeps is the part that matters for a real GTOS user image: the loader's text base, a separate data segment, and the
 * entry symbol the native-start stub jumps to.
 */
SECTIONS {
    . = 0x40000000;
    .text : { *(.text.entry) *(.text*) *(.rodata*) }
    . = 0x40200000;
    .data : { *(.data*) *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) *(.note.GNU-stack) }
}
