.section .text
.global _start
.extern skia_guest_main
_start:
  and $-16,%esp
  call skia_guest_main
  mov %eax,%ebx
  mov $0x4704,%eax
  int $0x80
  ud2
.section .note.GNU-stack,"",@progbits
