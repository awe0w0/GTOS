# Minimal freestanding entry for the TR/TSS host unit.
#
# A static i386 ELF with no libc supplies its own process entry. This unit takes no arguments: it builds its own table and
# reads it back, so the entry only has to run main and exit with its failure count.
#
# Only this host unit uses this entry. The kernel image never links it.

.section .text
.global _start
.extern trTssMain
_start:
    call trTssMain
    mov %eax, %ebx
    mov $1, %eax
    int $0x80
    hlt
