ENTRY(loader)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386:i386)

SECTIONS
{
    . = 0X0100000;
    kernel_start = .;

    .text :
    {
        *(.multiboot)
        *(.text*)
        *(.rodata*)
    }

    .data :
    {
        start_ctors = .;
        KEEP(*( .init_array));
        KEEP(*(SORT_BY_INIT_PRIORITY( .init_array.*)));
        end_ctors = .;

        *(.data*)
    }

    .bss :
    {
        *(.bss*)
        *(COMMON)
    }

    kernel_end = ALIGN(4096);

    /DISCARD/ :
    {
        *(.fini_array*)
        *(.comment)
    }
}