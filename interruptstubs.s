.set IRO_BASE, 0x20

.section .text

.extern _ZN17InterruptsManager15handleInterruptEhj
.global _ZN17InterruptsManager22IgnoreInterruptRequestEv

.macro HandleException num
.global _ZN17InterruptsManager16HandleException\num\()Ev
_ZN17InterruptsManager16HandleException\num\()Ev:
    movb $\num, (interruptnumber)
    jmp int_bottom
.endm

.macro HandleInterruptRequest num
.global _ZN17InterruptsManager26HandleInterruptRequest\num\()Ev
_ZN17InterruptsManager26HandleInterruptRequest\num\()Ev:
    movb $\num + IRO_BASE, (interruptnumber)
    jmp int_bottom
.endm

HandleInterruptRequest 0x00
HandleInterruptRequest 0x01
HandleInterruptRequest 0x0C

int_bottom:

    pusha
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs

    pushl %esp
    push (interruptnumber)
    call _ZN17InterruptsManager15handleInterruptEhj
    movl %eax, %esp

    popl %gs
    popl %fs
    popl %es
    popl %ds
    popa

_ZN17InterruptsManager22IgnoreInterruptRequestEv:

    iret
.data
    interruptnumber: .byte 0