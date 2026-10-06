.section .multiboot
.global native_fp_firmware_probe_header
native_fp_firmware_probe_header:
.long 0x1BADB002
.long 3
.long -(0x1BADB002 + 3)
.section .text
.global loader
.extern NativeFpFirmwareProbe
loader:
    cli
    cld
    movl $native_fp_firmware_probe_stack_top,%esp
    andl $-16,%esp
    subl $8,%esp
    pushl %eax
    pushl %ebx
    call NativeFpFirmwareProbe
1:  cli
    hlt
    jmp 1b
.section .bss
.balign 16
.space 16384
native_fp_firmware_probe_stack_top:
.section .note.GNU-stack,"",@progbits
