.set IRO_BASE, 0x20

.section .text

.extern _ZN4gtos21hardwarecommunication17InterruptsManager15handleInterruptEhj
.global _ZN4gtos21hardwarecommunication17InterruptsManager15InterruptIgnoreEv

.macro HandleException num
.global _ZN4gtos21hardwarecommunication17InterruptsManager19HandleException\num\()Ev
_ZN4gtos21hardwarecommunication17InterruptsManager19HandleException\num\()Ev:
    movb $\num, (interruptnumber)
    jmp int_bottom
.endm

.macro HandleInterruptRequest num
.global _ZN4gtos21hardwarecommunication17InterruptsManager26HandleInterruptRequest\num\()Ev
_ZN4gtos21hardwarecommunication17InterruptsManager26HandleInterruptRequest\num\()Ev:
    movb $\num + IRO_BASE, (interruptnumber)
    pushl $0
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

HandleInterruptRequest 0x00
HandleInterruptRequest 0x01
HandleInterruptRequest 0x02
HandleInterruptRequest 0x03
HandleInterruptRequest 0x04
HandleInterruptRequest 0x05
HandleInterruptRequest 0x06
HandleInterruptRequest 0x07
HandleInterruptRequest 0x08
HandleInterruptRequest 0x09
HandleInterruptRequest 0x0A
HandleInterruptRequest 0x0B
HandleInterruptRequest 0x0C
HandleInterruptRequest 0x0D
HandleInterruptRequest 0x0E
HandleInterruptRequest 0x0F
HandleInterruptRequest 0x31
HandleInterruptRequest 0x80

int_bottom:

    # pusha
    # pushl %ds
    # pushl %es
    # pushl %fs
    # pushl %gs

    // 按照multitasking中的CPUState结构体顺序入栈
    pushl %ebp
    pushl %edi
    pushl %esi

    pushl %edx
    pushl %ecx
    pushl %ebx
    pushl %eax

    # load ring 0 segment register laden
    # cld
    # mov $0x10, %eax
    # mov %eax. %eds
    # mov %eax, %ees

    # call cpp handler
    pushl %esp
    push (interruptnumber)
    movl %eax, 0
    call _ZN4gtos21hardwarecommunication17InterruptsManager15handleInterruptEhj
    # add %esp, 6
    movl %eax, %esp # 切获取到的esp来换栈

    popl %eax
    popl %ebx
    popl %ecx
    popl %edx

    popl %esi
    popl %edi
    popl %ebp
    
    # popl %gs
    # popl %fs
    # popl %es
    # popl %ds
    # popa

    add $4, %esp

_ZN4gtos21hardwarecommunication17InterruptsManager15InterruptIgnoreEv:

    iret
.data
    interruptnumber: .byte 0