# Minimal freestanding entry for the NativeRuntime host unit.
#
# A static i386 ELF with no libc supplies its own process entry. At _start the kernel has already placed argc at (%esp) and
# the argv vector just above it, so this entry forwards both: one ELF can then run one case per process, which is what the
# runtime needs because a successful Activate publishes a process-wide owner that has no destroy path.
#
# runtimeTestMain returns the number of failed expectations, and that becomes the process exit status.
#
# Only this host unit uses this entry. The kernel image never links it.

.section .text
.global _start
.extern runtimeTestMain
_start:
    mov (%esp), %eax        # argc
    lea 4(%esp), %ebx       # argv
    push %ebx
    push %eax
    call runtimeTestMain
    add $8, %esp
    mov %eax, %ebx
    mov $1, %eax
    int $0x80
    hlt
