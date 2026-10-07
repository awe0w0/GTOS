OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)
PHDRS {
  code PT_LOAD FILEHDR PHDRS FLAGS(5);
  record PT_LOAD FLAGS(6);
  data PT_LOAD FLAGS(6);
  stack PT_GNU_STACK FLAGS(6);
}
SECTIONS {
  . = 0x40000000 + SIZEOF_HEADERS;
  .text : { *(.text .text.*) *(.rodata .rodata.*) } :code
  ASSERT(ADDR(.text) + SIZEOF(.text) - 0x40000000 <= 0x20000, "probe RX exceeds existing 32 pages")
  . = 0x40020000;
  .vm_record : { KEEP(*(.data.vm_record)) } :record
  ASSERT(SIZEOF(.vm_record) == 24, "probe record must contain exactly six words")
  . = 0x40040000;
  .data : { *(.data .data.*) *(.got .got.*) } :data
  .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) } :data
  vm_probe_rw_limit = ALIGN(ADDR(.bss) + SIZEOF(.bss), 4096);
  ASSERT(SIZEOF(.data) + SIZEOF(.bss) <= 0xC0000, "probe RW exceeds existing 192 pages")
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
