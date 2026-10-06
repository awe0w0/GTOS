.section .text
.extern _ZN4gtos21hardwarecommunication17InterruptsManager15handleInterruptEhj
.global _ZN4gtos21hardwarecommunication17InterruptsManager15InterruptIgnoreEv

# Each frame owns its vector; no global state and no writes through address zero.
.macro HandleException num
.global _ZN4gtos21hardwarecommunication17InterruptsManager19HandleException\num\()Ev
_ZN4gtos21hardwarecommunication17InterruptsManager19HandleException\num\()Ev:
    .if (\num != 8) && (\num != 10) && (\num != 11) && (\num != 12) && (\num != 13) && (\num != 14) && (\num != 17)
        pushl $0
    .endif
    pushl $\num
    jmp int_bottom
.endm

.macro HandleInterruptRequest num, vector
.global _ZN4gtos21hardwarecommunication17InterruptsManager26HandleInterruptRequest\num\()Ev
_ZN4gtos21hardwarecommunication17InterruptsManager26HandleInterruptRequest\num\()Ev:
    pushl $0
    pushl $\vector
    jmp int_bottom
.endm

HandleException 0x00
HandleException 0x01
HandleException 0x02
HandleException 0x03
HandleException 0x04
HandleException 0x05
HandleException 0x06
HandleException 0x07
HandleException 0x08
HandleException 0x09
HandleException 0x0A
HandleException 0x0B
HandleException 0x0C
HandleException 0x0D
HandleException 0x0E
HandleException 0x0F
HandleException 0x10
HandleException 0x11
HandleException 0x12
HandleException 0x13

HandleInterruptRequest 0x00, 0x20
HandleInterruptRequest 0x01, 0x21
HandleInterruptRequest 0x02, 0x22
HandleInterruptRequest 0x03, 0x23
HandleInterruptRequest 0x04, 0x24
HandleInterruptRequest 0x05, 0x25
HandleInterruptRequest 0x06, 0x26
HandleInterruptRequest 0x07, 0x27
HandleInterruptRequest 0x08, 0x28
HandleInterruptRequest 0x09, 0x29
HandleInterruptRequest 0x0A, 0x2A
HandleInterruptRequest 0x0B, 0x2B
HandleInterruptRequest 0x0C, 0x2C
HandleInterruptRequest 0x0D, 0x2D
HandleInterruptRequest 0x0E, 0x2E
HandleInterruptRequest 0x0F, 0x2F
HandleInterruptRequest 0x31, 0x51
HandleInterruptRequest 0x80, 0x80

int_bottom:
    pushl %ebp
    pushl %edi
    pushl %esi
    pushl %edx
    pushl %ecx
    pushl %ebx
    pushl %eax
    cld
    movl %esp, %edx
    movl 28(%edx), %eax
    andl $-16, %esp
    subl $8, %esp
    pushl %edx
    pushl %eax
    call _ZN4gtos21hardwarecommunication17InterruptsManager15handleInterruptEhj
    movl %eax, %esp
    popl %eax
    popl %ebx
    popl %ecx
    popl %edx
    popl %esi
    popl %edi
    popl %ebp
    addl $8, %esp
_ZN4gtos21hardwarecommunication17InterruptsManager15InterruptIgnoreEv:
    iret
.section .note.GNU-stack,"",@progbits
