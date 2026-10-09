.section .multiboot
.long 0x1BADB002
.long 3
.long -(0x1BADB002 + 3)
.section .text
.global loader
.extern NativeProcessSmoke
loader:
    cli
    cld
    movl $filesystem_stack_top,%esp
    andl $-16,%esp
    subl $8,%esp
    pushl %eax
    pushl %ebx
    call NativeProcessSmoke
1: cli; hlt; jmp 1b
.section .bss
.balign 16
.global filesystem_stack_bottom,filesystem_stack_top
filesystem_stack_bottom:
.space 16384
filesystem_stack_top:
.section .note.GNU-stack,"",@progbits
