.section .text
.code32
# These are the only production FP instructions. All buffers are supervisor
# owned and resident. No waiting x87 opcode is permitted in this file.
.global native_fp_probe_asm
.type native_fp_probe_asm,@function
native_fp_probe_asm:
    movl 4(%esp), %eax
    clts
    fxsave (%eax)
    fninit
    fnstenv 512(%eax)
    movl %cr0, %eax
    orl $8, %eax
    movl %eax, %cr0
    ret
.size native_fp_probe_asm, .-native_fp_probe_asm

.global native_fp_save_asm
.type native_fp_save_asm,@function
native_fp_save_asm:
    movl 4(%esp), %eax
    # Entry validated TS=0. Do not conceal a broken owner by clearing TS here.
    fxsave (%eax)
    fnstenv 512(%eax)
    ret
.size native_fp_save_asm, .-native_fp_save_asm

.global native_fp_neutral_asm
.type native_fp_neutral_asm,@function
native_fp_neutral_asm:
    movl 4(%esp), %eax
    clts
    fninit
    fldenv 512(%eax)
    fxrstor (%eax)
    movl %cr0, %eax
    orl $8, %eax
    movl %eax, %cr0
    ret
.size native_fp_neutral_asm, .-native_fp_neutral_asm

.global native_fp_restore_asm
.type native_fp_restore_asm,@function
native_fp_restore_asm:
    movl 4(%esp), %eax
    movl 4(%eax), %edx
    clts
    fninit
    fldenv 512(%edx)
    fxrstor (%edx)
    addl $1, 12(%eax)
    movl $2, (%eax)
    # No C++ or waiting FP operation follows: caller is the IRET epilogue.
    ret
.size native_fp_restore_asm, .-native_fp_restore_asm
.section .note.GNU-stack,"",@progbits
