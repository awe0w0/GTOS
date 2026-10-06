.section .text
.code32
.global _start
_start:
    cmpl $0xdecabeef, elf_data
    jne failure
    cmpl $1, elf_mode
    je text_write
    # BSS spans multiple pages and must start zeroed.
    movl $elf_bss,%esi
    movl $8192,%ecx
1:  cmpb $0,(%esi)
    jne failure
    incl %esi
    decl %ecx
    jnz 1b
    movl $0x1ee7c0de,elf_bss
    movl $0x4701,%eax
    movl $message,%ebx
    movl $6,%ecx
    int $0x80
    cmpl $6,%eax
    jne failure
    movl $0x4702,%eax
    int $0x80
    movl %eax,%ebp
2:  movl $200000,%ecx
3:  incl elf_progress
    decl %ecx
    jnz 3b
    cmpl $0xdecabeef,elf_data
    jne failure
    cmpl $0x1ee7c0de,elf_bss
    jne failure
    movl $0x4702,%eax
    int $0x80
    subl %ebp,%eax
    cmpl $20,%eax
    jb 2b
    movl $0x4704,%eax
    xorl %ebx,%ebx
    int $0x80
text_write:
    movb $0,_start
failure:
    movl $0x4704,%eax
    movl $99,%ebx
    int $0x80
    ud2
message: .ascii "ELF32\n"
.section .data
.global elf_data
elf_data: .long 0xdecabeef
elf_mode: .long 0
elf_progress: .long 0
.section .bss
.balign 16
elf_bss: .space 8192
.section .note.GNU-stack,"",@progbits
