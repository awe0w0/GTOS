    .text
    .code32
    .globl _start
_start:
    andl $-16, %esp
    call gtos_setjmp_checks
    # Report the result before exiting through the host's i386 emulator.
    movl %eax, %ebp
    pushl %eax
    movl %esp, %ecx
    movl $4, %eax
    movl $1, %ebx
    movl $4, %edx
    int $0x80
    movl %ebp, %ebx
    movl $1, %eax
    int $0x80
    ud2

    .globl check_saved_registers
    .type check_saved_registers,@function
check_saved_registers:
    pushl %ebp
    pushl %edi
    pushl %esi
    pushl %ebx
    subl $24, %esp
    movl $0x11223344, %ebx
    movl $0x55667788, %esi
    movl $0x13579bdf, %edi
    movl $0x2468ace0, %ebp
    movl %esp, %edx
    pushl %edx
    call setjmp
    addl $4, %esp
    testl %eax, %eax
    jnz .Lresumed
    xorl %ebx, %ebx
    xorl %esi, %esi
    xorl %edi, %edi
    xorl %ebp, %ebp
    movl %esp, %edx
    subl $12, %esp
    pushl $77
    pushl %edx
    call longjmp
    ud2
.Lresumed:
    cmpl $77, %eax
    jne .Lfailed
    cmpl $0x11223344, %ebx
    jne .Lfailed
    cmpl $0x55667788, %esi
    jne .Lfailed
    cmpl $0x13579bdf, %edi
    jne .Lfailed
    cmpl $0x2468ace0, %ebp
    jne .Lfailed
    xorl %eax, %eax
    jmp .Ldone
.Lfailed:
    movl $1, %eax
.Ldone:
    addl $24, %esp
    popl %ebx
    popl %esi
    popl %edi
    popl %ebp
    ret
    .size check_saved_registers,.-check_saved_registers

    .section .note.GNU-stack,"",@progbits
