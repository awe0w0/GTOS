/* BIOS Multiboot2 i386 entry. No dependency on the existing i386 kernel. */
.section .multiboot2,"a"
.balign 8
header:
.long 0xe85250d6, 0, header_end-header
.long -(0xe85250d6 + (header_end-header))
.short 1, 0                    /* Required information request: memory map */
.long 12, 6
.balign 8
.short 0, 0
.long 8
header_end:

.section .boot.text,"ax"
.code32
.global boot32
boot32:
    cli
    cld
    mov %eax,%esi
    mov %ebx,%ebp
    /* External PC NMI mask as early as possible; EAX was saved above. */
    mov $0x80,%al
    outb %al,$0x70
    /* Multiboot promises flat data segments and paging disabled. */
    mov $__bss_start,%edi
    mov $__kernel_end,%ecx
    sub %edi,%ecx
    xor %eax,%eax
    rep stosb
    lgdt gdt_ptr
    ljmp $8,$own_segments
own_segments:
    mov $16,%ax
    mov %ax,%ds
    mov %ax,%es
    mov %ax,%ss
    mov $stack_top,%esp
    pushl $2
    popfl                    /* Own NT/AC/IOPL/DF state, IF remains clear. */
    mov %esi,boot_magic
    mov %ebp,boot_info
    mov $idt32,%edi
    mov $256,%ecx
1:  mov $boot_exception,%eax
    movw %ax,(%edi)
    movw $8,2(%edi)
    movw $0x8e00,4(%edi)
    shr $16,%eax
    movw %ax,6(%edi)
    add $8,%edi
    loop 1b
    lidt idt32_ptr
#if TEST_INJECT == 1
    movl $0,boot_magic
#elif TEST_INJECT == 2
    orl $1,boot_info
#elif TEST_INJECT == 7
    movl $0,boot_info
#elif TEST_INJECT == 8
    movl $0x4000000,boot_info
#elif TEST_INJECT == 9
    mov boot_info,%eax
    movl $15,(%eax)
#elif TEST_INJECT == 10
    mov boot_info,%eax
    movl $65544,(%eax)
#elif TEST_INJECT == 11
    mov boot_info,%eax
    movl $17,(%eax)
#elif TEST_INJECT == 12
    movl $fake_header,boot_info
#elif TEST_INJECT == 20
    mov $0x10,%eax
    mov %eax,%cr4
#elif TEST_INJECT == 22
    mov $fake_tss,%eax
    movw $103,gdt+32
    movw %ax,gdt+34
    shr $16,%eax
    movb %al,gdt+36
    movb $0x89,gdt+37
    movb $0,gdt+38
    movb %ah,gdt+39
    mov $32,%ax
    ltr %ax
#elif TEST_INJECT == 13
    /* Runner gives this negative image 128 MiB; these bytes are private guest RAM. */
    movl $0x3fff000,boot_info
    movl $8192,0x3fff000
#endif
    cmpl $0x36d76289,boot_magic
    jne bad_handoff
    mov %cr0,%eax
    and $0x80000001,%eax
    cmp $1,%eax
    jne bad_state
    mov %cr4,%eax
    test %eax,%eax
    jnz bad_state
    str %ax
    test %ax,%ax
    jnz bad_state           /* Do not inherit any cached task-state descriptor. */
    /* Qualify CPUID before use. */
    pushfl
    pop %eax
    mov %eax,%ecx
    xor $0x200000,%eax
    push %eax
    popfl
    pushfl
    pop %eax
    push %ecx
    popfl
    xor %ecx,%eax
    test $0x200000,%eax
    jz bad_cpu
    xor %eax,%eax
    cpuid
    cmp $1,%eax
    jb bad_cpu
    mov $1,%eax
    cpuid
    and $0x260,%edx          /* MSR, PAE and local APIC required */
    cmp $0x260,%edx
    jne bad_cpu
    mov $0x80000000,%eax
    cpuid
    cmp $0x80000001,%eax
    jb bad_cpu
    mov $0x80000001,%eax
    cpuid
    and $0x20100000,%edx     /* LM and NX required */
    cmp $0x20100000,%edx
    jne bad_cpu
    mov $0x1b,%ecx
    rdmsr
    test $0x100,%eax         /* Only BSP is permitted to run this entry */
    jz bad_state
    mov $0xc0000080,%ecx
#if TEST_INJECT == 21
    rdmsr
    or $0x800,%eax
    wrmsr
#endif
    rdmsr
    or %edx,%eax
    jnz bad_state           /* Deliberately bounded clean EFER handoff */
    mov boot_info,%esi
    test $7,%esi
    jnz bad_handoff
    cmp $0x1000,%esi
    jb bad_handoff
    cmp $0x3fffff8,%esi
    ja bad_handoff
    mov (%esi),%ecx
    cmp $16,%ecx
    jb bad_handoff
    cmp $65536,%ecx
    ja bad_handoff
    test $7,%ecx
    jnz bad_handoff
    mov %esi,%edi
    add %ecx,%edi
    jc bad_handoff
    cmp $0x4000000,%edi
    ja bad_handoff
    cmp $__kernel_end,%esi
    jae 2f
    cmp $__kernel_start,%edi
    ja bad_handoff
2:  mov %ecx,boot_size
    /* Build only the low 64 MiB hierarchy; most leaves remain absent. */
    mov $pdpt,%eax
    or $3,%eax
    mov %eax,pml4
    mov $pd,%eax
    or $3,%eax
    mov %eax,pdpt
    xor %ecx,%ecx
3:  mov %ecx,%eax
    shl $12,%eax
    add $pt,%eax
    or $3,%eax
    mov %eax,pd(,%ecx,8)
    inc %ecx
    cmp $32,%ecx
    jne 3b
    mov $__text_start,%esi
    mov $__text_end,%edi
    mov $1,%eax
    xor %edx,%edx
    call map_range
    mov $__rodata_start,%esi
    mov $__rodata_end,%edi
    mov $1,%eax
    mov $0x80000000,%edx
    call map_range
    mov $__data_start,%esi
    mov $__kernel_end,%edi
    mov $3,%eax
    call map_range
    mov boot_info,%esi
    mov %esi,%edi
    add boot_size,%edi
    and $-4096,%esi
    add $4095,%edi
    and $-4096,%edi
    mov $1,%eax
    call map_range
    /* Separate absent lower guards for every runtime stack. */
    mov $stack_guard,%esi
    call unmap_page
    mov $fault_guard,%esi
    call unmap_page
    mov $df_guard,%esi
    call unmap_page
    mov $nmi_guard,%esi
    call unmap_page
    mov $mc_guard,%esi
    call unmap_page
    /* Build mode-appropriate emergency gates before enabling paging. */
    mov $idt64,%edi
    mov $256,%ecx
4:  mov $early_exception64,%eax
    movw %ax,(%edi)
    movw $24,2(%edi)
    movw $0x8e00,4(%edi)
    shr $16,%eax
    movw %ax,6(%edi)
    movl $0,8(%edi)
    movl $0,12(%edi)
    add $16,%edi
    loop 4b
    mov $0x20,%eax
    mov %eax,%cr4
    mov $pml4,%eax
    mov %eax,%cr3
    mov $0xc0000080,%ecx
    rdmsr
    or $0x900,%eax           /* LME | NXE */
    wrmsr
    mov %cr0,%eax
    or $0x8001000c,%eax      /* PG | WP | EM | TS (integer-only target) */
    mov %eax,%cr0
    ljmp $24,$long_entry
map_range:
    cmp %edi,%esi
    jae 6f
5:  mov %esi,%ecx
    shr $12,%ecx
    mov %esi,%ebx
    or %eax,%ebx
    mov %ebx,pt(,%ecx,8)
    mov %edx,pt+4(,%ecx,8)
    add $4096,%esi
    cmp %edi,%esi
    jb 5b
6:  ret
unmap_page:
    shr $12,%esi
    movl $0,pt(,%esi,8)
    movl $0,pt+4(,%esi,8)
    ret
bad_handoff: mov $msg_handoff,%esi; jmp fail32
bad_cpu: mov $msg_cpu,%esi; jmp fail32
bad_state: mov $msg_state,%esi; jmp fail32
boot_exception: mov $msg_exception,%esi
fail32:
    lodsb
    test %al,%al
    jz 7f
    outb %al,$0xe9
    jmp fail32
7:  mov $0x11,%eax
    outl %eax,$0xf4
8:  cli; hlt; jmp 8b
.code64
long_entry:
    lidt idt64_ptr(%rip)
    mov $16,%ax
    mov %ax,%ds
    mov %ax,%es
    mov %ax,%ss
    xor %eax,%eax
    mov %ax,%fs
    mov %ax,%gs
    mov $stack_top,%rsp
    xor %ebp,%ebp
    call kernel_main
    jmp early_exception64
.global early_exception64
early_exception64:
    mov $msg_exception,%esi
9:  lodsb
    test %al,%al
    jz 10f
    outb %al,$0xe9
    jmp 9b
10: mov $0x11,%eax
    outl %eax,$0xf4
11: cli; hlt; jmp 11b

.section .rodata
msg_handoff: .asciz "X64 FAIL boot handoff\n"
msg_cpu: .asciz "X64 FAIL required CPU features\n"
msg_state: .asciz "X64 FAIL unsupported entry state\n"
msg_exception: .asciz "X64 FAIL early exception\n"
.balign 8
gdt_ptr: .word gdt_end-gdt-1; .quad gdt
idt32_ptr: .word 256*8-1; .quad idt32
idt64_ptr: .word 256*16-1; .quad idt64
.section .data
.balign 4096
.global gdt
gdt:
.quad 0
.quad 0x00cf9b000000ffff     /* accessed 32-bit code */
.quad 0x00cf93000000ffff     /* accessed data */
.quad 0x00af9b000000ffff     /* L=1 D=0 accessed 64-bit code */
.quad 0,0                  /* 16-byte TSS descriptor, initialized in C */
gdt_end:
#if TEST_INJECT == 12
.balign 8
fake_header: .long 16,0,0,8
#endif
.section .bss
.balign 8
.global boot_magic,boot_info,boot_size
boot_magic: .skip 4
boot_info: .skip 4
boot_size: .skip 4
#if TEST_INJECT == 22
.balign 16
fake_tss: .skip 104
#endif
.balign 4096
.global pml4,pdpt,pd,pt,idt64
pml4: .skip 4096
pdpt: .skip 4096
pd: .skip 4096
pt: .skip 32*4096
idt32: .skip 256*8
.balign 4096
idt64: .skip 256*16
.macro stack name, size
.balign 4096
.global \name\()_guard,\name\()_bottom,\name\()_top
\name\()_guard: .skip 4096
\name\()_bottom: .skip \size
\name\()_top:
.endm
stack stack,16384
stack fault,16384
stack df,8192
stack nmi,8192
stack mc,8192
.section .note.GNU-stack,"",@progbits
