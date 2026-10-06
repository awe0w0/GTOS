.section .text
.extern _ZN4gtos21hardwarecommunication17InterruptsManager15handleInterruptEhj
.global _ZN4gtos21hardwarecommunication17InterruptsManager15InterruptIgnoreEv

# Each frame owns its vector; no global state and no writes through address zero.
.macro HandleException num
.global _ZN4gtos21hardwarecommunication17InterruptsManager19HandleException\num\()Ev
_ZN4gtos21hardwarecommunication17InterruptsManager19HandleException\num\()Ev:
    .if (\num != 8) && (\num != 10) && (\num != 11) && (\num != 12) && (\num != 13) && (\num != 14) && (\num != 17) && (\num != 21) && (\num != 29) && (\num != 30)
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
HandleException 0x14
HandleException 0x15
HandleException 0x16
HandleException 0x17
HandleException 0x18
HandleException 0x19
HandleException 0x1A
HandleException 0x1B
HandleException 0x1C
HandleException 0x1D
HandleException 0x1E
HandleException 0x1F

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
    # Save selectors as full initialized words: push %ds can leave high bits dirty.
    xorl %eax, %eax
    movw %ds, %ax
    pushl %eax
    movw %es, %ax
    pushl %eax
    movw %fs, %ax
    pushl %eax
    movw %gs, %ax
    pushl %eax
    movw $0x18, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    cld
    # Save/neutralize BEFORE ordinary C++, including no-switch syscalls.
    movl %esp, %ebx
    andl $-16, %esp
    subl $12, %esp
    pushl %ebx
    call native_fp_enter_trap
    addl $16, %esp
    subl $8, %esp
    pushl %ebx
    pushl 44(%ebx)
    call _ZN4gtos21hardwarecommunication17InterruptsManager15handleInterruptEhj
    # Prepare using the selected frame/CR3, still with kernel selectors and TS.
    movl %eax, %ebx
    addl $16, %esp
    subl $12, %esp
    pushl %ebx
    call native_fp_prepare_return
    addl $16, %esp
    testl %eax, %eax
    jz 1f
    subl $12, %esp
    pushl %eax
    call native_fp_restore_asm
    # NO C++ follows restoration or publication of UserLive.
1:
    movl %ebx, %esp
    popl %gs
    popl %fs
    popl %es
    popl %ds
    popl %eax
    popl %ebx
    popl %ecx
    popl %edx
    popl %esi
    popl %edi
    popl %ebp
    addl $8, %esp
    iret
_ZN4gtos21hardwarecommunication17InterruptsManager15InterruptIgnoreEv:
    pushl $0
    pushl $255
    jmp int_bottom
.section .note.GNU-stack,"",@progbits
