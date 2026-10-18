.section .text.entry,"ax"
.code32
.global NativeClipboardStart
.extern NativeClipboardEntry
.type NativeClipboardStart,@function
NativeClipboardStart:
    andl $-16, %esp
    call NativeClipboardEntry
    ud2
.size NativeClipboardStart,.-NativeClipboardStart
.section .note.GNU-stack,"",@progbits
