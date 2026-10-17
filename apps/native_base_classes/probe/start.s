.section .text.entry,"ax"
.code32
.global NativeClassStart
.extern NativeClassEntry
.type NativeClassStart,@function
NativeClassStart:
    andl $-16, %esp
    call NativeClassEntry
    ud2
.size NativeClassStart,.-NativeClassStart
.section .note.GNU-stack,"",@progbits
