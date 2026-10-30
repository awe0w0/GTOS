/*
 * Entry stub for a consumer that returns a status instead of trapping.
 *
 * The shared native-start.s ends with `ud2` because the probes it was written for never return: they call the platform's exit service themselves.
 * A consumer that returns an integer (this file probe returns a failure bitmask) would run off that stub into the trap, and the guest reports
 * vector 6 — an invalid opcode — instead of the process's own status. That is exactly what happened on 2026-10-31: BROWSER PROBE EXIT 80000006.
 * This stub turns a return value into the GTOS process exit (GTOS_SYS_EXIT, 0x4704, status in EBX), so the guest records the code the consumer
 * computed rather than a fault it did not cause.
 */
.section .text.entry,"ax"
.code32
.global _start
.extern NativeEntry
.type _start,@function
_start:
    andl $-16, %esp
    call NativeEntry
    movl %eax, %ebx
    movl $0x4704, %eax
    int $0x80
    ud2
.size _start,.-_start
.section .note.GNU-stack,"",@progbits
