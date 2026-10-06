# Real CPL3 payload. All FP instructions are deliberately explicit assembly.
# First two instructions observe the inherited hardware before any FP load.
.section .text
.code32
.global native_fp_user_start, native_fp_user_end
.set DATA, 0x40002000
.set INITIAL, DATA+64
.set INITIAL_ENV, DATA+576
.set EXPECTED, DATA+608
.set EXPECTED_ENV, DATA+1120
.set ACTUAL, DATA+1152
.set ACTUAL_ENV, DATA+1664
.set XMM_PATTERN, DATA+1696
.set MM_PATTERN, DATA+1824
.set X87_PATTERN, DATA+1888
.set CONTROL, DATA+2016
.set MXCSR, DATA+2020
.set PENDING_ENV, DATA+2048
.set SYS_ABI, 0x4700
.set SYS_WRITE, 0x4701
.set SYS_TICKS, 0x4702
.set SYS_YIELD, 0x4703
.set SYS_EXIT, 0x4704
.type native_fp_user_start,@function
native_fp_user_start:
    fxsave INITIAL
    fnstenv INITIAL_ENV
    # Independent emulator/hardware semantics probe, before any kernel trap.
    # A synthetic full environment must reload all four pointer/selector fields.
    movl $INITIAL_ENV,%esi
    movl $PENDING_ENV,%edi
    movl $7,%ecx
    rep movsl
    movl $0x12345678,PENDING_ENV+12
    movw $0x23,PENDING_ENV+16
    movl $0x34567890,PENDING_ENV+20
    movw $0x2b,PENDING_ENV+24
    fldenv PENDING_ENV
    fnstenv ACTUAL_ENV
    cmpl $0x12345678,ACTUAL_ENV+12
    je 8f
    orl $1,DATA+44
8:  cmpw $0x23,ACTUAL_ENV+16
    je 9f
    orl $2,DATA+44
9:  cmpl $0x34567890,ACTUAL_ENV+20
    je 10f
    orl $4,DATA+44
10: cmpw $0x2b,ACTUAL_ENV+24
    je 11f
    orl $8,DATA+44
11: fninit
    fldenv INITIAL_ENV
    fxrstor INITIAL
    movl $1,DATA+8
    cmpl $1,DATA
    je native_fp_success
    call native_fp_seed
    call native_fp_compare
    movl DATA,%eax
    cmpl $0,%eax
    je native_fp_peer
    cmpl $2,%eax
    je native_fp_pending_mf
    cmpl $3,%eax
    je native_fp_simd_fault
    cmpl $4,%eax
    je native_fp_bad_save_alignment
    cmpl $5,%eax
    je native_fp_bad_restore_alignment
    cmpl $6,%eax
    je native_fp_bad_save_cross_page
    cmpl $7,%eax
    je native_fp_bad_restore_hole
    cmpl $8,%eax
    je native_fp_bad_supervisor
    cmpl $9,%eax
    je native_fp_bad_mxcsr
    cmpl $10,%eax
    je native_fp_avx
    cmpl $11,%eax
    je native_fp_bad_page
    cmpl $12,%eax
    je native_fp_bad_opcode
    cmpl $13,%eax
    je native_fp_success
    cmpl $14,%eax
    je native_fp_mmx
    cmpl $16,%eax
    je native_fp_simd_direct
    cmpl $17,%eax
    je native_fp_bad_ldmxcsr
    jmp native_fp_fail

native_fp_peer:
    # No syscall, voluntary yield, or FP reload anywhere in this long window.
    # Kernel checks runTicks and dispatches while systemCalls is still zero.
    movl $2,DATA+8
    movl $120000000,%ebp
1:  decl %ebp
    jnz 1b
    call native_fp_compare
    orl $1,DATA+28
native_fp_cycle:
    movl $3,DATA+8
    movl $SYS_ABI,%eax
    int $0x80
    cmpl $1,%eax
    jne native_fp_fail
    call native_fp_compare
    movl $SYS_TICKS,%eax
    int $0x80
    movl %eax,DATA+32
    call native_fp_compare
    # Accepted one-byte NUL write exercises copying/printing without log spam.
    movl $SYS_WRITE,%eax
    movl $DATA+60,%ebx
    movl $1,%ecx
    int $0x80
    cmpl $1,%eax
    jne native_fp_fail
    call native_fp_compare
    movl $SYS_WRITE,%eax
    movl $DATA+4095,%ebx
    movl $2,%ecx
    int $0x80
    cmpl $-14,%eax
    jne native_fp_fail
    call native_fp_compare
    movl $SYS_WRITE,%eax
    movl $DATA,%ebx
    movl $257,%ecx
    int $0x80
    cmpl $-7,%eax
    jne native_fp_fail
    call native_fp_compare
    orl $2,DATA+28
    # Legal null data selectors and DF survive a switch with seeded FP live.
    movl $4,DATA+8
    xorl %eax,%eax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%fs
    movw %ax,%gs
    std
    movl $SYS_YIELD,%eax
    int $0x80
    testl %eax,%eax
    jne native_fp_bad_segments
    movw %ds,%ax
    testw %ax,%ax
    jne native_fp_bad_segments
    movw %es,%ax
    testw %ax,%ax
    jne native_fp_bad_segments
    movw %fs,%ax
    testw %ax,%ax
    jne native_fp_bad_segments
    movw %gs,%ax
    testw %ax,%ax
    jne native_fp_bad_segments
    pushfl
    popl %eax
    testl $0x400,%eax
    jz native_fp_bad_segments
    cld
    movw $0x2b,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%fs
    movw %ax,%gs
    call native_fp_compare
    orl $4,DATA+28
    # All eight MMX registers alias the x87 register file, by design.
native_fp_mmx:
    movl $5,DATA+8
    movq MM_PATTERN+0,%mm0
    movq MM_PATTERN+8,%mm1
    movq MM_PATTERN+16,%mm2
    movq MM_PATTERN+24,%mm3
    movq MM_PATTERN+32,%mm4
    movq MM_PATTERN+40,%mm5
    movq MM_PATTERN+48,%mm6
    movq MM_PATTERN+56,%mm7
    call native_fp_capture
    cmpl $14,DATA
    je native_fp_success
    movl $SYS_TICKS,%eax
    int $0x80
    movl %eax,DATA+2688
14: movl $12000000,%ebp
2:  decl %ebp
    jnz 2b
    movl $SYS_TICKS,%eax
    int $0x80
    subl DATA+2688,%eax
    cmpl $3,%eax
    jb 14b
    call native_fp_compare
    movl $SYS_YIELD,%eax
    int $0x80
    call native_fp_compare
    emms
    fnstenv ACTUAL_ENV
    cmpw $0xffff,ACTUAL_ENV+8
    jne native_fp_fail
    fildl DATA+4
    fadd %st(0),%st(0)
    fistpl DATA+36
    movl DATA+4,%eax
    addl %eax,%eax
    cmpl %eax,DATA+36
    jne native_fp_fail
    orl $8,DATA+28
    incl DATA+12
    call native_fp_seed
    jmp native_fp_cycle

native_fp_seed:
    fninit
    fldcw CONTROL
    fldt X87_PATTERN+0
    fldt X87_PATTERN+16
    fldt X87_PATTERN+32
    fldt X87_PATTERN+48
    fldt X87_PATTERN+64
    fldt X87_PATTERN+80
    fldt X87_PATTERN+96
    fldt X87_PATTERN+112
    # Distinct nonzero TOP and full tag shapes for the two independent peers.
    fincstp
    testl $1,DATA+4
    jz 3f
    fincstp
    ffree %st(3)
    jmp 4f
3:  ffree %st(5)
4:  # Exercise AMD MM only when the CPU's own FXSAVE mask advertises it.
    movl INITIAL+28,%eax
    andl $0x20000,%eax
    orl %eax,MXCSR
    ldmxcsr MXCSR
    movdqu XMM_PATTERN+0,%xmm0
    movdqu XMM_PATTERN+16,%xmm1
    movdqu XMM_PATTERN+32,%xmm2
    movdqu XMM_PATTERN+48,%xmm3
    movdqu XMM_PATTERN+64,%xmm4
    movdqu XMM_PATTERN+80,%xmm5
    movdqu XMM_PATTERN+96,%xmm6
    movdqu XMM_PATTERN+112,%xmm7
    call native_fp_capture
    cmpl $1,DATA+8
    jne 7f
    fxsave DATA+2080
    fnstenv DATA+2592
    fldenv DATA+2592
7:  ret
native_fp_capture:
    fxsave EXPECTED
    fnstenv EXPECTED_ENV
    fldenv EXPECTED_ENV
    ret

native_fp_compare:
    pushal
    fxsave ACTUAL
    fnstenv ACTUAL_ENV
    fldenv ACTUAL_ENV
    movl $EXPECTED,%esi
    movl $ACTUAL,%edi
    # Defined control/status/tag, MXCSR, eight 80-bit and eight 128-bit values.
    movl (%esi),%eax
    cmpl (%edi),%eax
    jne native_fp_fail
    movb 4(%esi),%al
    cmpb 4(%edi),%al
    jne native_fp_fail
    movl 24(%esi),%eax
    cmpl 24(%edi),%eax
    jne native_fp_fail
    addl $32,%esi
    addl $32,%edi
    movl $8,%edx
5:  movl $10,%ecx
    repe cmpsb
    jne native_fp_fail
    addl $6,%esi
    addl $6,%edi
    decl %edx
    jnz 5b
    movl $128,%ecx
    repe cmpsb
    jne native_fp_fail
    # FNSTENV independently observes pointers on AMD ES=0 implementations.
    movl $EXPECTED_ENV,%esi
    movl $ACTUAL_ENV,%edi
    movw 0(%esi),%ax
    cmpw 0(%edi),%ax
    jne native_fp_fail
    movw 4(%esi),%ax
    cmpw 4(%edi),%ax
    jne native_fp_fail
    movw 8(%esi),%ax
    cmpw 8(%edi),%ax
    jne native_fp_fail
    movw 18(%esi),%ax
    xorw 18(%edi),%ax
    testw $0x7ff,%ax
    jnz native_fp_fail
    # Explicit opt-in diagnostic mode may waive only semantics independently
    # proven absent above. Default strict tests never waive pointer fidelity.
    cmpl $1,DATA+48
    jne 12f
    cmpl $15,DATA+44
    je 13f
12: movl 12(%esi),%eax
    cmpl 12(%edi),%eax
    jne native_fp_fail
    movw 16(%esi),%ax
    cmpw 16(%edi),%ax
    jne native_fp_fail
    movl 20(%esi),%eax
    cmpl 20(%edi),%eax
    jne native_fp_fail
    movw 24(%esi),%ax
    cmpw 24(%edi),%ax
    jne native_fp_fail
13: popal
    ret

native_fp_pending_mf:
    # Synthetic pending invalid operation in a user-owned full environment.
    # FLDENV and all subsequent work are non-waiting until explicit FWAIT.
    fnstenv PENDING_ENV
    andw $0xfffe,PENDING_ENV
    orw $0x8081,PENDING_ENV+4
    fldenv PENDING_ENV
    movl $6,DATA+8
    movl $SYS_ABI,%eax
    int $0x80
    cmpl $1,%eax
    jne native_fp_fail
    movl $SYS_YIELD,%eax
    int $0x80
    movl $120000000,%ebp
6:  decl %ebp
    jnz 6b
    fxsave ACTUAL
    fnstenv ACTUAL_ENV
    fldenv ACTUAL_ENV
    testw $1,ACTUAL
    jnz native_fp_fail
    testw $0x80,ACTUAL+2
    jz native_fp_fail
    testw $1,ACTUAL+2
    jz native_fp_fail
    movl $7,DATA+8
    fwait
    jmp native_fp_fail
native_fp_simd_fault:
    movl $0x1f00,MXCSR # invalid unmasked, all other exceptions masked
    ldmxcsr MXCSR
    movl $SYS_ABI,%eax
    int $0x80
    movl $SYS_YIELD,%eax
    int $0x80
    stmxcsr DATA+40
    cmpl $0x1f00,DATA+40
    jne native_fp_fail
    jmp native_fp_simd_arithmetic
native_fp_simd_direct:
    movl $0x1f00,MXCSR
    ldmxcsr MXCSR
native_fp_simd_arithmetic:
    movl $8,DATA+8
    pxor %xmm0,%xmm0
    divps %xmm0,%xmm0
    stmxcsr DATA+40
    # Distinct diagnostic result proves the real arithmetic instruction returned.
    movl $0xF019,%ebx
    movl $SYS_EXIT,%eax
    int $0x80
    ud2
native_fp_bad_save_alignment:
    fxsave ACTUAL+1
    jmp native_fp_fail
native_fp_bad_restore_alignment:
    fxrstor EXPECTED+1
    jmp native_fp_fail
native_fp_bad_save_cross_page:
    fxsave DATA+3840
    jmp native_fp_fail
native_fp_bad_restore_hole:
    fxrstor DATA+4096
    jmp native_fp_fail
native_fp_bad_supervisor:
    movl DATA+16,%eax
    fxsave (%eax)
    jmp native_fp_fail
native_fp_bad_mxcsr:
    orl $0x80000000,EXPECTED+24
    fxrstor EXPECTED
    stmxcsr DATA+40
    movl $0xF00D,%ebx
    movl $SYS_EXIT,%eax
    int $0x80
    ud2
native_fp_bad_ldmxcsr:
    orl $0x80000000,MXCSR
    ldmxcsr MXCSR
    stmxcsr DATA+40
    movl $0xF00D,%ebx
    movl $SYS_EXIT,%eax
    int $0x80
    ud2
native_fp_avx:
    # VEX.128 VXORPS xmm0,xmm0,xmm0; AVX advertised, OSXSAVE withheld.
    .byte 0xc5,0xf8,0x57,0xc0
    jmp native_fp_fail
native_fp_bad_page:
    movl $0,DATA+4096
    jmp native_fp_fail
native_fp_bad_opcode:
    ud2
    jmp native_fp_fail
native_fp_bad_segments:
    cld
    movw $0x2b,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%fs
    movw %ax,%gs
native_fp_fail:
    movl $0xfbad,DATA+20
    movl $0xfbad,%ebx
    movl $SYS_EXIT,%eax
    int $0x80
    ud2
native_fp_success:
    xorl %ebx,%ebx
    movl $SYS_EXIT,%eax
    int $0x80
    ud2
native_fp_user_end:
.size native_fp_user_start, .-native_fp_user_start
.section .note.GNU-stack,"",@progbits
