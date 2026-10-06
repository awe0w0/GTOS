// Minimal Linux syscall runner for the shared C++ store tests; no libc needed.
#include <common/types.h>
extern "C" int main();
extern "C" int puts(const char* text) {
    uint32_t length=0;while(text[length])length++;
    const char newline='\n';
#ifdef __x86_64__
    uint64_t result;
    asm volatile("syscall":"=a"(result):"a"(1),"D"(1),"S"(text),"d"(length):"rcx","r11","memory");
    asm volatile("syscall":"=a"(result):"a"(1),"D"(1),"S"(&newline),"d"(1):"rcx","r11","memory");
#else
    { uint32_t written; asm volatile("int $0x80" : "=a"(written) : "0"(4),"b"(1),"c"(text),"d"(length) : "memory", "cc"); }
    { uint32_t written; asm volatile("int $0x80" : "=a"(written) : "0"(4),"b"(1),"c"(&newline),"d"(1) : "memory", "cc"); }
#endif
    return 0;
}
#ifdef __x86_64__
asm(".text\n.global _start\n_start:\nxor %rbp,%rbp\nand $-16,%rsp\ncall StoreEntry\n");
extern "C" void StoreEntry() {
    int result=main();
    asm volatile("syscall"::"a"(60),"D"(result):"rcx","r11","memory");
    __builtin_unreachable();
}
#else
extern "C" void _start() {
    int result=main();
    asm volatile("int $0x80"::"a"(1),"b"(result):"memory");
    __builtin_unreachable();
}
#endif
