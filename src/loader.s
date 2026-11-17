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

    # Keep the v1 entry for existing BIOS images. EFI loaders can use v2
    # to deliver the ACPI root, the memory map and the linear framebuffer.
    .balign 8
.Lmultiboot2:
    .long 0xe85250d6, 0, .Lmultiboot2_end - .Lmultiboot2
    .long -(0xe85250d6 + (.Lmultiboot2_end - .Lmultiboot2))
    .short 5, 1
    .long 20, 800, 600, 32
    .balign 8
    .short 6, 0
    .long 8
    .short 0, 0
    .long 8
.Lmultiboot2_end:

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
