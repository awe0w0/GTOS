.section .text.native_crypto_probe_call,"ax",@progbits
.p2align 4
.globl native_crypto_probe_call
.type native_crypto_probe_call,@function
native_crypto_probe_call:
    pushl %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    subl $12, %esp
    pushl 44(%esp)
    pushl 44(%esp)
    pushl 44(%esp)
    pushl 44(%esp)
    movl $0x13579bdf, %ebx
    movl $0x2468ace0, %esi
    movl $0x55aa33cc, %edi
    movl $0x1234abcd, %ebp
    call native_crypto_md5
    addl $28, %esp
    xorl %edx, %edx
    cmpl $1, %eax
    sete %dl
    cmpl $0x13579bdf, %ebx
    jne 1f
    orl $2, %edx
1:
    cmpl $0x2468ace0, %esi
    jne 2f
    orl $4, %edx
2:
    cmpl $0x55aa33cc, %edi
    jne 3f
    orl $8, %edx
3:
    cmpl $0x1234abcd, %ebp
    jne 4f
    orl $16, %edx
4:
    movl %edx, %eax
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    ret
.size native_crypto_probe_call,.-native_crypto_probe_call
.section .note.GNU-stack,"",@progbits
