ENTRY(loader)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386:i386)
SECTIONS {
    . = 0x100000;
    kernel_start = .;
    kernel_readonly_start = .;
    .text : { *(.multiboot) *(.text*) *(.rodata*) }
    . = ALIGN(4096);
    kernel_readonly_end = .;
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
    kernel_end = ALIGN(4096);
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
