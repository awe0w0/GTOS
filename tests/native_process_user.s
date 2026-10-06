# Independently assembled position-fixed user fixture, copied as raw bytes.
# Every absolute data reference is in its private mapping, never a kernel symbol.
.section .text
.code32
.global native_user_start, native_user_end
.set DATA, 0x40002000
.set SYS_ABI, 0x4700
.set SYS_WRITE, 0x4701
.set SYS_TICKS, 0x4702
.set SYS_YIELD, 0x4703
.set SYS_EXIT, 0x4704
native_user_start:
    movw %cs, %ax
    andl $3, %eax
    cmpl $3, %eax
    jne fail
    movl $SYS_ABI, %eax
    int $0x80
    cmpl $1, %eax
    jne fail
    movl DATA, %ebp
    movl DATA+4, %eax
    cmpl $0, %eax
    je survivor
    cmpl $1, %eax
    je kernel_write
    cmpl $2, %eax
    je bad_cli
    cmpl $3, %eax
    je bad_hlt
    cmpl $4, %eax
    je bad_io
    cmpl $5, %eax
    je bad_ud
    cmpl $6, %eax
    je bad_div
    cmpl $7, %eax
    je bad_text
    cmpl $8, %eax
    je bad_stack
    cmpl $9, %eax
    je bad_fp
    cmpl $10, %eax
    je bad_gate
    cmpl $11, %eax
    je syscalls
    cmpl $12,%eax
    je probe_12
    cmpl $13,%eax
    je probe_13
    cmpl $14,%eax
    je probe_14
    cmpl $15,%eax
    je probe_15
    cmpl $16,%eax
    je probe_16
    cmpl $17,%eax
    je probe_17
    cmpl $18,%eax
    je probe_18
    cmpl $19,%eax
    je probe_19
    cmpl $20,%eax
    je probe_20
    cmpl $21,%eax
    je probe_21
    cmpl $22,%eax
    je probe_22
    cmpl $23,%eax
    je probe_23
    cmpl $24,%eax
    je probe_24
    cmpl $25,%eax
    je probe_25
    cmpl $26,%eax
    je probe_26
    cmpl $27,%eax
    je probe_27
    jmp fail
survivor:
    # Entire hot loop has no syscall, yield or HLT. Progress of the kernel and
    # other tasks proves actual timer preemption.
    cmpl DATA, %ebp
    jne fail
    incl DATA+12
    jmp survivor
kernel_write:
    movl DATA+8, %edi
    movl $0xbadc0de, (%edi)
    jmp fail
bad_cli: cli; jmp fail
bad_hlt: hlt; jmp fail
bad_io: movb $65,%al; outb %al,$0xe9; jmp fail
bad_ud: ud2; jmp fail
bad_div: xorl %edx,%edx; xorl %ecx,%ecx; movl $1,%eax; divl %ecx; jmp fail
bad_text: movb $0,0x40000000; jmp fail
bad_stack: movl $0,0xbfffcffc; jmp fail
bad_fp: fninit; jmp fail
bad_gate: int $0x20; jmp fail
syscalls:
    # All unsuccessful output must be atomic. Last byte of the data page is X,
    # the following page is absent; there must be no X on the debug stream.
    movl $0xffffffff,%eax
    int $0x80
    cmpl $-38,%eax
    jne fail
    movl $SYS_WRITE,%eax
    xorl %ebx,%ebx
    movl $1,%ecx
    int $0x80
    cmpl $-14,%eax
    jne fail
    movl $SYS_WRITE,%eax
    movl DATA+8,%ebx
    movl $1,%ecx
    int $0x80
    cmpl $-14,%eax
    jne fail
    movl $SYS_WRITE,%eax
    movl $0xfee00000,%ebx
    movl $1,%ecx
    int $0x80
    cmpl $-14,%eax
    jne fail
    movl $SYS_WRITE,%eax
    movl $0xfffffffe,%ebx
    movl $4,%ecx
    int $0x80
    cmpl $-14,%eax
    jne fail
    movl $SYS_WRITE,%eax
    movl $0x40002fff,%ebx
    movl $2,%ecx
    int $0x80
    cmpl $-14,%eax
    jne fail
    movl $SYS_WRITE,%eax
    movl $0x40002fff,%ebx
    movl $257,%ecx
    int $0x80
    cmpl $-7,%eax
    jne fail
    # Empty writes still require an in-arena address; mapped bytes are not read.
    movl $SYS_WRITE,%eax
    movl $0x40001000,%ebx
    xorl %ecx,%ecx
    int $0x80
    testl %eax,%eax
    jne fail
    movl $SYS_WRITE,%eax
    xorl %ebx,%ebx
    xorl %ecx,%ecx
    int $0x80
    cmpl $-14,%eax
    jne fail
    # Valid buffer straddles the two private stack pages.
    movl $0x73736150,0xbfffdffe # 'Pass'
    movb $10,0xbfffe002
    movl $SYS_WRITE,%eax
    movl $0xbfffdffe,%ebx
    movl $5,%ecx
    int $0x80
    cmpl $5,%eax
    jne fail
    # Legacy syscall4 must NOT reach raw EBX printf at CPL3.
    movl $4,%eax
    movl DATA+8,%ebx
    int $0x80
    cmpl $-38,%eax
    jne fail
    # Kernel entry must normalize null DS/ES/FS/GS and DF, then restore them.
    xorl %eax,%eax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%fs
    movw %ax,%gs
    std
    movl $SYS_YIELD,%eax
    movl $0x11223344,%ebx
    movl $0x55667788,%ecx
    movl $0xaabbccdd,%edx
    movl $0x12345678,%esi
    movl $0x87654321,%edi
    int $0x80
    cmpl $0,%eax
    jne fail
    cmpl $0x11223344,%ebx
    jne fail
    cmpl $0x55667788,%ecx
    jne fail
    cmpl $0xaabbccdd,%edx
    jne fail
    cmpl $0x12345678,%esi
    jne fail
    cmpl $0x87654321,%edi
    jne fail
    movw %ds,%ax
    testw %ax,%ax
    jne fail
    movw %es,%ax
    testw %ax,%ax
    jne fail
    movw %fs,%ax
    testw %ax,%ax
    jne fail
    movw %gs,%ax
    testw %ax,%ax
    jne fail
    pushfl
    popl %eax
    testl $0x400,%eax
    jz fail
    cld
    movw $0x2b,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%fs
    movw %ax,%gs
    # Stay with null segments across a *timer* IRQ, no syscall assistance.
    xorl %eax,%eax
    movw %ax,%ds
    movw %ax,%es
    std
    movl $12000000,%ecx
null_spin:
    decl %ecx
    jnz null_spin
    pushfl
    popl %eax
    testl $0x400,%eax
    jz fail
    movw %ds,%ax
    testw %ax,%ax
    jne fail
    movw %es,%ax
    testw %ax,%ax
    jne fail
    cld
    movw $0x2b,%ax
    movw %ax,%ds
    movw %ax,%es
    movl $SYS_TICKS,%eax
    int $0x80
    testl %eax,%eax
    jz fail
    movl $SYS_EXIT,%eax
    xorl %ebx,%ebx
    int $0x80
fail:
    movl $SYS_EXIT,%eax
    movl $99,%ebx
    int $0x80
    ud2

probe_12:
    pxor %xmm0,%xmm0
    jmp fail
probe_13:
    pxor %mm0,%mm0
    jmp fail
probe_14:
    fxsave DATA+16
    jmp fail
probe_15:
    fwait
    jmp fail
probe_16:
    pushfl; orl $0x4000,(%esp); popfl; movl $SYS_ABI,%eax; int $0x80; pushfl; popl %eax; testl $0x4000,%eax; jnz fail; jmp probe_ok
    jmp fail
probe_17:
    movl $SYS_ABI,%eax; pushfl; orl $0x100,(%esp); popfl; int $0x80; jmp probe_ok
    jmp fail
# QEMU 8.2 reports a user #DB after MOV SS here; newer QEMU suppresses
# that TF trap before INT clears TF. The smoke permits only that exact
# contained outcome (mode18), never an unexpected/kernel-origin fault.
probe_18:
    movl $SYS_ABI,%eax; movw $0x2b,%dx; pushfl; orl $0x100,(%esp); popfl; movw %dx,%ss; int $0x80; jmp probe_ok
    jmp fail
probe_19:
    movw $0x18,%ax; movw %ax,%ss
    jmp fail
probe_20:
    movw $0x23,%ax; movw %ax,%ds; movw %ax,%es; movw %ax,%fs; movw %ax,%gs; movl $SYS_ABI,%eax; int $0x80; movw %gs,%ax; cmpw $0x23,%ax; jne fail; jmp probe_ok
    jmp fail
probe_21:
    xorl %esp,%esp; movl $SYS_ABI,%eax; int $0x80; jmp probe_ok
    jmp fail
probe_22:
    movw $0xffff,%dx; inl %dx,%eax
    jmp fail
probe_23:
    pushl $0x10; pushl $0x40000000; lret
    jmp fail
probe_24:
    pushfl; orl $0x4000,(%esp); popfl; iret
    jmp fail
probe_25:
    sysenter
    jmp fail
probe_26:
    syscall
    jmp fail
probe_27:
    sysret
    jmp fail
probe_ok:
    movl $SYS_EXIT,%eax
    xorl %ebx,%ebx
    int $0x80
    ud2
native_user_end:
.section .note.GNU-stack,"",@progbits
