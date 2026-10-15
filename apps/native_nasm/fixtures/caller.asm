%include "constants.inc"
BITS 32
section .text
global GtosNasmCaller:function
extern GtosNasmProvider
extern gtos_nasm_value
GtosNasmCaller:
    mov eax, [esp + 4]
    add eax, [gtos_nasm_value]
    add eax, GTOS_NASM_BIAS
    push eax
    call GtosNasmProvider
    add esp, 4
    ret
section .note.GNU-stack noalloc noexec nowrite progbits
