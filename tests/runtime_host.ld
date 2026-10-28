/* GTOS-DSH-141 host unit link layout.
 *
 * The runtime host unit exercises the production paging object, and that object refuses an unsafe context unless its own
 * bytes, the allocator's bytes, the directory and every table are identity mapped. On the modelled machine the kernel image
 * sits at 1 MiB and that is where the identity map covers, so the unit has to load there too: linking at the conventional
 * 0x08048000 put the object outside its own map, and the production check -- correctly -- refused it.
 *
 * This script changes where the unit loads. It does not change what the unit does, and it is used by this host unit only; the
 * kernel image keeps its own script. The modelled read-only page still starts at 1 MiB, so the first page here is the page
 * the model marks read-only, and everything the model must write to lands past it.
 */
ENTRY(_start)
SECTIONS
{
  . = 0x00100000;
  __gtos_host_image_start = .;
  .text : { *(.text*) *(.gtos_kernel) }
  . = ALIGN(4096);
  .rodata : { *(.rodata*) *(.gtos_ro_start) *(.gtos_ro_end) }
  . = ALIGN(4096);
  .data : { *(.data*) }
  . = ALIGN(4096);
  .bss : { *(.bss*) *(COMMON) }
  . = ALIGN(4096);
  __gtos_host_image_end = .;
  /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}