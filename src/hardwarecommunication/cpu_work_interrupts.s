.section .text
.code32
.extern gtos_work_ap_interrupt, gtos_work_ap_fault
.macro AP_EXCEPTION num
.Lap_exception_\num:
    .if (\num != 8) && (\num != 10) && (\num != 11) && (\num != 12) && (\num != 13) && (\num != 14) && (\num != 17) && (\num != 21) && (\num != 29) && (\num != 30)
        pushl $0
    .endif
    pushl $\num
    jmp .Lap_fault_common
.endm
.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    AP_EXCEPTION \n
.endr
.global gtos_work_wake_interrupt, gtos_work_spurious_interrupt, gtos_work_unexpected_interrupt
gtos_work_wake_interrupt:
    pushl $0
    pushl $0xF0
    jmp .Lap_interrupt_common
gtos_work_spurious_interrupt:
    pushl $0
    pushl $0xFF
    jmp .Lap_interrupt_common
gtos_work_unexpected_interrupt:
    pushl $0
    pushl $0xFE
.Lap_fault_common:
    cli
    pushal
    cld
    movl %esp,%eax
    andl $-16,%esp
    subl $12,%esp
    pushl %eax
    call gtos_work_ap_fault
1:  cli
    hlt
    jmp 1b
.Lap_interrupt_common:
    pushal
    cld
    movl %esp,%eax
    andl $-16,%esp
    subl $12,%esp
    pushl %eax
    call gtos_work_ap_interrupt
    movl %eax,%esp
    popal
    addl $8,%esp
    iret
.section .rodata
.balign 4
.global gtos_work_exception_table
gtos_work_exception_table:
.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    .long .Lap_exception_\n
.endr
.section .note.GNU-stack,"",@progbits
