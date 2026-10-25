# Minimal freestanding entry for the CpuWorkPool host unit.
#
# A static i386 ELF with no libc supplies its own process entry. poolTestMain returns the number of failed
# expectations, and that value becomes the process exit status through the i386 exit syscall.
#
# Only this host unit uses this entry. The kernel image never links it.

.section .text
.global _start
.extern poolTestMain
_start:
    call poolTestMain
    mov %eax, %ebx
    mov $1, %eax
    int $0x80
    hlt