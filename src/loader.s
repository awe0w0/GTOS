.set MAGIC, 0x1badb002
.set FLAGS, (1 << 0 | 1 << 1 | 1 << 2)
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
    .long MAGIC
    .long FLAGS
    .long CHECKSUM
    # Address fields occupy their specified slots even without a.out flag16.
    .long 0, 0, 0, 0, 0
    # Multiboot v1 preferred linear RGB framebuffer: graphics,800x600x32.
    .long 0, 800, 600, 32

.section .text
.extern kernelMain
.extern callConstructors
.global loader

loader:
    cli
    cld
    mov $kernel_stack, %esp
    andl $-16, %esp
    subl $8, %esp
    xorl %ebp, %ebp

    push %eax
    push %ebx
    call callConstructors
    call kernelMain

_stop:
    cli
    hlt
    jmp _stop

.section .bss
.space 4 * 1024 * 1024 #4MB
kernel_stack:
.section .note.GNU-stack,"",@progbits
