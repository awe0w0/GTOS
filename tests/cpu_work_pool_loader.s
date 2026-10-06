.section .multiboot
.long 0x1BADB002
.long 3
.long -(0x1BADB002 + 3)
.section .text
.global loader
.extern WorkPoolSmoke
loader:
    cli
    cld
    movl $work_smoke_stack_top,%esp
    andl $-16,%esp
    subl $8,%esp
    pushl %eax
    pushl %ebx
    call WorkPoolSmoke
1:  cli
    hlt
    jmp 1b
.global WorkSmokeTimerEntry
.extern workSmokeTicks
WorkSmokeTimerEntry:
    pushal
    cld
    incl workSmokeTicks
    movb $0x20,%al
    outb %al,$0x20
    popal
    iret
.global WorkSmokeUnexpectedEntry
.extern WorkSmokeUnexpected
WorkSmokeUnexpectedEntry:
    cli
    cld
    andl $-16,%esp
    call WorkSmokeUnexpected
    jmp 1b
.section .bss
.balign 16
.space 1024 * 1024
work_smoke_stack_top:
.section .note.GNU-stack,"",@progbits
