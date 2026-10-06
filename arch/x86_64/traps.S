.section .text
.code64
.extern handle_trap
/* All architectural exceptions 0..31. Error-code vectors are normalized. */
.macro isr number, error=0
.global isr\number
isr\number:
.if !\error
    pushq $0
.endif
    pushq $\number
    jmp trap_common
.endm
isr 0
isr 1
isr 2
isr 3
isr 4
isr 5
isr 6
isr 7
isr 8,1
isr 9
isr 10,1
isr 11,1
isr 12,1
isr 13,1
isr 14,1
isr 15
isr 16
isr 17,1
isr 18
isr 19
isr 20
isr 21,1
isr 22
isr 23
isr 24
isr 25
isr 26
isr 27
isr 28
isr 29,1
isr 30,1
isr 31
trap_common:
    cld
    push %rax
    push %rcx
    push %rdx
    push %rbx
    push %rbp
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp,%rdi
    mov %rsp,%rbx
    and $-16,%rsp
    call handle_trap
    mov %rbx,%rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rbp
    pop %rbx
    pop %rdx
    pop %rcx
    pop %rax
    add $16,%rsp
    iretq
.global probe_write,probe_write_ip,probe_write_resume
probe_write:
probe_write_ip: movb $0,(%rdi)
probe_write_resume: ret
.global probe_read,probe_read_ip,probe_read_resume
probe_read:
probe_read_ip: mov (%rdi),%rax
probe_read_resume: ret
.global probe_execute,probe_execute_resume
probe_execute: jmp *%rdi
probe_execute_resume: ret
.global probe_stack,probe_stack_ip,probe_stack_resume
probe_stack:
    mov %rsp,%r11
    lea stack_bottom(%rip),%rsp
probe_stack_ip:
    push %rax
probe_stack_resume:
    mov %r11,%rsp
    ret
.global probe_ud,probe_ud_ip,probe_ud_resume
probe_ud:
probe_ud_ip: ud2
probe_ud_resume: ret
.section .rodata
.global isr_table
isr_table:
.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
.quad isr\n
.endr
.section .note.GNU-stack,"",@progbits
