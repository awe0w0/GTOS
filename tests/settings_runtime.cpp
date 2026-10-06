// Freestanding Linux test runner. Syscall return registers are outputs: a write
// returns its byte count in EAX/RAX, not the syscall number for the next write.
#include <common/types.h>
extern "C" int main();

extern "C" int puts(const char* text) {
    uint32_t length=0; while(text[length]) length++;
    const char newline='\n';
#ifdef __x86_64__
    uint64_t result;
    asm volatile("syscall":"=a"(result):"a"(1),"D"(1),"S"(text),"d"(length):"rcx","r11","memory");
    asm volatile("syscall":"=a"(result):"a"(1),"D"(1),"S"(&newline),"d"(1):"rcx","r11","memory");
#else
    uint32_t result;
    asm volatile("int $0x80":"=a"(result):"a"(4),"b"(1),"c"(text),"d"(length):"memory");
    asm volatile("int $0x80":"=a"(result):"a"(4),"b"(1),"c"(&newline),"d"(1):"memory");
#endif
    return 0;
}

#ifdef __x86_64__
asm(".text\n.global _start\n_start:\nxor %rbp,%rbp\nand $-16,%rsp\ncall SettingsTestEntry\n");
#else
asm(".text\n.global _start\n_start:\nxor %ebp,%ebp\nand $-16,%esp\ncall SettingsTestEntry\n");
#endif
extern "C" void SettingsTestEntry() {
    int result=main();
#ifdef __x86_64__
    asm volatile("syscall"::"a"(60),"D"(result):"rcx","r11","memory");
#else
    asm volatile("int $0x80"::"a"(1),"b"(result):"memory");
#endif
    __builtin_unreachable();
}
