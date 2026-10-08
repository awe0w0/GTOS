OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)
PHDRS {
  code PT_LOAD FILEHDR PHDRS FLAGS(5);
  constants PT_LOAD FLAGS(4);
  record PT_LOAD FLAGS(6);
  data PT_LOAD FLAGS(6);
  stack PT_GNU_STACK FLAGS(6);
}
SECTIONS {
  . = 0x40000000 + SIZEOF_HEADERS;
  .text : { *(.text .text.*) } :code
  ASSERT(ADDR(.text) + SIZEOF(.text) - 0x40000000 <= 0x20000, "probe RX exceeds existing 32 pages")
  . = ALIGN(4096);
  .rodata.gtos_emutls_templates : {
    __gtos_emutls_templates_start = .;
    KEEP(*(SORT_BY_NAME(.rodata.__emutls_t.*)))
    __gtos_emutls_templates_end = .;
  } :constants
  .rodata : { *(.rodata .rodata.*) } :constants
  . = 0x40030000;
  .tls_record : { KEEP(*(.data.tls_record)) } :record
  ASSERT(SIZEOF(.tls_record) == 512, "TLS record must contain exactly 512 bytes")
  . = 0x40040000;
  .gtos_emutls_controls : {
    __gtos_emutls_controls_start = .;
    KEEP(*(SORT_BY_NAME(.data.__emutls_v.*)))
    __gtos_emutls_controls_end = .;
  } :data
  .data : { *(.data .data.*) *(.got .got.*) } :data
  .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) } :data
  tls_probe_rw_limit = ALIGN(ADDR(.bss) + SIZEOF(.bss), 4096);
  ASSERT(SIZEOF(.data) + SIZEOF(.bss) + SIZEOF(.gtos_emutls_controls) <= 0xC0000, "probe RW exceeds existing 192 pages")
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
