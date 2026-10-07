.section .text
.global resource_probe_registers
.type resource_probe_registers,@function
// int resource_probe_registers(operation, first, second): expect EAX=0 and
// preserve all non-result GPRs and DS/ES/FS/GS across an actual int 0x80.
resource_probe_registers:
    push %ebp
    push %edi
    push %esi
    push %ebx
    sub $20,%esp
    xor %eax,%eax
    mov %ds,%ax
    mov %eax,4(%esp)
    mov %es,%ax
    mov %eax,8(%esp)
    mov %fs,%ax
    mov %eax,12(%esp)
    mov %gs,%ax
    mov %eax,16(%esp)
    mov 40(%esp),%eax
    mov 44(%esp),%ebx
    mov 48(%esp),%ecx
    mov $0x13579bdf,%edx
    mov $0x2468ace0,%esi
    mov $0x31415926,%edi
    mov $0x27182818,%ebp
    int $0x80
    test %eax,%eax
    jne .Lfail
    cmp 44(%esp),%ebx
    jne .Lfail
    cmp 48(%esp),%ecx
    jne .Lfail
    cmp $0x13579bdf,%edx
    jne .Lfail
    cmp $0x2468ace0,%esi
    jne .Lfail
    cmp $0x31415926,%edi
    jne .Lfail
    cmp $0x27182818,%ebp
    jne .Lfail
    xor %eax,%eax
    mov %ds,%ax
    cmp 4(%esp),%eax
    jne .Lfail
    mov %es,%ax
    cmp 8(%esp),%eax
    jne .Lfail
    mov %fs,%ax
    cmp 12(%esp),%eax
    jne .Lfail
    mov %gs,%ax
    cmp 16(%esp),%eax
    jne .Lfail
    mov $1,%eax
    jmp .Ldone
.Lfail:
    xor %eax,%eax
.Ldone:
    add $20,%esp
    pop %ebx
    pop %esi
    pop %edi
    pop %ebp
    ret
.size resource_probe_registers,.-resource_probe_registers
.section .note.GNU-stack,"",@progbits
