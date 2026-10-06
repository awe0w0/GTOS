.section .text.entry,"ax"
.code32
.global _start
.extern NativeEntry
_start:
    andl $-16, %esp
    call NativeEntry
    ud2
.section .note.GNU-stack,"",@progbits
