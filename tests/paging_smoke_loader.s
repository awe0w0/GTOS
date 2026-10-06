.section .multiboot
.long 0x1BADB002
.long 3
.long -(0x1BADB002 + 3)
.section .text
.global loader
.extern PagingSmoke
loader:
    cli
    cld
    movl $paging_test_stack_top, %esp
    andl $-16, %esp
    subl $8, %esp
    pushl %eax
    pushl %ebx
    call PagingSmoke
1:
    cli
    hlt
    jmp 1b
.global PagingFaultEntry
.extern PagingFault
PagingFaultEntry:
    cli
    cld
    movl (%esp), %eax
    movl %cr2, %edx
    andl $-16, %esp
    subl $8, %esp
    pushl %edx
    pushl %eax
    call PagingFault
    jmp 1b
.global PagingUnexpectedEntry
.extern PagingUnexpected
PagingUnexpectedEntry:
    cli
    cld
    andl $-16, %esp
    call PagingUnexpected
    jmp 1b
.section .bss
.balign 16
.space 1024 * 1024
paging_test_stack_top:
.section .note.GNU-stack,"",@progbits
