OUTPUT_FORMAT(elf64-x86-64)
ENTRY(boot32)
PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
 . = 0x100000;
 __kernel_start = .;
 __text_start = .;
 .text : { KEEP(*(.multiboot2)) *(.boot.text) *(.text .text.*) . = ALIGN(4096); } :text
 . = ALIGN(4096); __text_end = .;
 __rodata_start = .;
 .rodata : { *(.rodata .rodata.*) . = ALIGN(4096); } :rodata
 . = ALIGN(4096); __rodata_end = .;
 __data_start = .;
 .data : { *(.data .data.*) } :data
 . = ALIGN(4096); __bss_start = .;
 .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) . = ALIGN(4096); } :data
 . = ALIGN(4096); __kernel_end = .;
 /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
 ASSERT(__kernel_end < 0x400000, "x64 bring-up image must stay below 4 MiB")
}
