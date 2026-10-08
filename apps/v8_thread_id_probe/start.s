 .section .text.start,"ax",@progbits
.global _start
.type _start,@function
_start:
    and $-16,%esp
    call tls_guest_main
    mov %eax,%ebx
    mov $0x4704,%eax
    int $0x80
    ud2
.size _start,.-_start
.section .note.GNU-stack,"",@progbits
