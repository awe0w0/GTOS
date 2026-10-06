.section .text
.global _start
.type _start, @function
_start:
    xorl %ebp, %ebp
    andl $-16, %esp
    call browser_probe_main
    movl %eax, %ebx
    movl $0x4704, %eax
    int $0x80
    ud2
.size _start, .-_start
.section .note.GNU-stack,"",@progbits
