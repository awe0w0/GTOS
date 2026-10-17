.section .text.crypto_abi_control,"ax",@progbits
.p2align 4
.globl __wrap_native_crypto_md5
.type __wrap_native_crypto_md5,@function
__wrap_native_crypto_md5:
    subl $12, %esp
    pushl 28(%esp)
    pushl 28(%esp)
    pushl 28(%esp)
    pushl 28(%esp)
    call __real_native_crypto_md5
    addl $28, %esp
    xorl $1, %ebx
    ret
.size __wrap_native_crypto_md5,.-__wrap_native_crypto_md5
.section .note.GNU-stack,"",@progbits
