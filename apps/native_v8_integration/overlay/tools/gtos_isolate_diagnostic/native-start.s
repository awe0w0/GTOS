.section .text.entry,"ax"
.code32
.global _start
.extern NativeEntry
.type _start,@function
_start:
    andl $-16, %esp
    call NativeEntry
    ud2
.size _start,.-_start
.section .note.GNU-stack,"",@progbits
