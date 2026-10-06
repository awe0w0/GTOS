.section .multiboot
.long 0x1BADB002
.long 3
.long -(0x1BADB002 + 3)
.section .text
.global loader
.extern CpuStartupSmoke
loader:
    cli
    cld
    movl $ap_test_stack_top, %esp
    andl $-16, %esp
    subl $8, %esp
    pushl %eax
    pushl %ebx
    call CpuStartupSmoke
1:
    cli
    hlt
    jmp 1b
.section .bss
.balign 16
.space 1024 * 1024
ap_test_stack_top:
.section .note.GNU-stack,"",@progbits
