# Relocatable one-page SIPI trampoline. The BSP patches only the GDT base,
# protected-mode far target and immutable shared-parameters pointer.
.section .rodata.ap_trampoline,"a",@progbits
.balign 16
.code16
.global gtos_ap_trampoline_start, gtos_ap_trampoline_end
.global gtos_ap_trampoline_gdt, gtos_ap_trampoline_gdtr
.global gtos_ap_trampoline_jump, gtos_ap_trampoline_parameters
.global gtos_ap_trampoline_protected
gtos_ap_trampoline_start:
    cli
    cld
    movw %cs, %ax
    movw %ax, %ds
    movzwl %ax, %ebp
    shll $4, %ebp
    lgdtl (gtos_ap_trampoline_gdtr - gtos_ap_trampoline_start)
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmpl *(gtos_ap_trampoline_jump - gtos_ap_trampoline_start)

.code32
gtos_ap_trampoline_protected:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl (gtos_ap_trampoline_parameters - gtos_ap_trampoline_start)(%ebp), %esi
    movl $1, %eax
    cpuid
    shrl $24, %ebx
    movl 8(%esi,%ebx,4), %esp
    testl %esp, %esp
    jz 1f
    andl $-16, %esp
    movl 4(%esi), %eax
    lidt (%eax)
    subl $12, %esp
    pushl 1032(%esi,%ebx,4)
    call *(%esi)
1:
    cli
    hlt
    jmp 1b
.balign 8
gtos_ap_trampoline_gdt:
    .quad 0
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
gtos_ap_trampoline_gdtr:
    .word 23
    .long 0
gtos_ap_trampoline_jump:
    .long 0
    .word 0x08
gtos_ap_trampoline_parameters:
    .long 0
gtos_ap_trampoline_end:

# AP-private emergency IDT gates land here. Never touch BSP exception globals
# and never return through a possibly malformed exception frame.
.section .text
.code32
.global gtos_ap_fault_halt
gtos_ap_fault_halt:
    cli
2:
    hlt
    jmp 2b
.section .note.GNU-stack,"",@progbits
