# Minimal freestanding entry for the boot-diagnostic host unit.
#
# The unit is a static i386 ELF with no libc, so it supplies its own process entry.
# bootDiagnosticTestMain returns the number of failed expectations; that value becomes the
# process exit status through the i386 exit syscall, so a zero exit means every check passed.
#
# Only the host unit uses this entry. The kernel image never links it.

.section .text
.global _start
.extern bootDiagnosticTestMain
_start:
    call bootDiagnosticTestMain
    mov %eax, %ebx
    mov $1, %eax
    int $0x80
    hlt
