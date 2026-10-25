# Minimal freestanding entry for the CpuWorkQueue host unit.
#
# A static i386 ELF with no libc supplies its own process entry. queueTestMain returns the number of failed
# expectations, and that value becomes the process exit status through the i386 exit syscall, so a zero exit means
# every check passed.
#
# Only this host unit uses this entry. The kernel image never links it.

.section .text
.global _start
.extern queueTestMain
_start:
    call queueTestMain
    mov %eax, %ebx
    mov $1, %eax
    int $0x80
    hlt