# Minimal freestanding entry for the safe-TSS host unit.

.section .text
.global _start
.extern oMain
_start:
    call oMain
    mov %eax, %ebx
    mov $1, %eax
    int $0x80
    hlt
