.section .text
.code32
# Diagnostic ELF only: exact bounded symbol, never a production allowance.
# No CALL, syscall, STI or waiting x87 instruction occurs in this qualifier.
# Reject IF=1; the caller holds InterruptGuard. All buffers are kernel BSS.
.global native_fp_desktop_pointer_probe_asm
.type native_fp_desktop_pointer_probe_asm,@function
native_fp_desktop_pointer_probe_asm:
    movl 4(%esp),%ecx
    pushfl
    popl %edx
    movl %edx,2720(%ecx)
    testl $0x200,%edx
    jnz 1f
    movl %cr0,%edx
    movl %edx,2728(%ecx)
    andl $12,%edx
    cmpl $8,%edx
    jne 1f
    movl %cr4,%edx
    movl %edx,2736(%ecx)
    andl $0x600,%edx
    cmpl $0x600,%edx
    jne 1f
    clts
    fxsave 0(%ecx)
    fnstenv 512(%ecx)
    fninit
    fldenv 1056(%ecx)
    fxsave 1088(%ecx)
    fnstenv 1600(%ecx)
    fninit
    fxrstor 1632(%ecx)
    fxsave 2176(%ecx)
    fnstenv 2688(%ecx)
    # Restore the original canonical neutral image before leaving the probe.
    fninit
    fldenv 512(%ecx)
    fxrstor 0(%ecx)
    movl 2728(%ecx),%edx
    movl %edx,%cr0
    movl %cr0,%edx
    movl %edx,2732(%ecx)
    movl %cr4,%edx
    movl %edx,2740(%ecx)
    pushfl
    popl %edx
    movl %edx,2724(%ecx)
    movl $1,%eax
    ret
1:  xorl %eax,%eax
    ret
.size native_fp_desktop_pointer_probe_asm,.-native_fp_desktop_pointer_probe_asm
.section .note.GNU-stack,"",@progbits
