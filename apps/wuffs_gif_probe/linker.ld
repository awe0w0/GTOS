OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)
PHDRS {
  code PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
  stack PT_GNU_STACK FLAGS(6);
}
SECTIONS {
  . = 0x40000000;
  .text : { *(.text .text.*) *(.rodata .rodata.*) } :code
  ASSERT(SIZEOF(.text) <= 0x20000, "probe RX exceeds 32 pages")
  . = 0x40040000;
  .data : { *(.data .data.*) } :data
  .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) } :data
  ASSERT(SIZEOF(.data) + SIZEOF(.bss) <= 0xC0000, "probe RW exceeds 192 pages")
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
