.section .multiboot
.long 0x1BADB002
.long 3
.long -(0x1BADB002 + 3)
.section .text
.global loader
.extern NativeFpSmoke
loader:
    cli
    cld
    movl $native_fp_test_stack_top,%esp
    andl $-16,%esp
    subl $8,%esp
    pushl %eax
    pushl %ebx
    call NativeFpSmoke
1: cli; hlt; jmp 1b
# Deliberate guard violation, used only by a separately asserted panic boot.
.global native_fp_test_kernel_misuse
.type native_fp_test_kernel_misuse,@function
native_fp_test_kernel_misuse:
.if NATIVE_FP_KERNEL_MISUSE
    fninit
.else
    ud2
.endif
    ret
.size native_fp_test_kernel_misuse, .-native_fp_test_kernel_misuse
.section .bss
.balign 16
.space 1024 * 1024
native_fp_test_stack_top:
.section .note.GNU-stack,"",@progbits
