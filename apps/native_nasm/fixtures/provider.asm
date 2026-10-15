BITS 32
section .text
global GtosNasmProvider:function
GtosNasmProvider:
    mov eax, [esp + 4]
    add eax, 3
    ret
section .data
global gtos_nasm_value:data
gtos_nasm_value: dd 11
section .note.GNU-stack noalloc noexec nowrite progbits
