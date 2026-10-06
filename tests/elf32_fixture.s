# Assembled locally; validation fixture only, never executed as a host program.
.section .text
.global _start
_start:
    xorl %eax, %eax
1:  nop
    jmp 1b
.section .data
.global fixture_value
fixture_value:
    .long 0x12345678
.section .bss
.balign 16
.global fixture_zero
fixture_zero:
    .skip 8192
.section .note.GNU-stack,"",@progbits
