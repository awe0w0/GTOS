#include <process/abi.h>
#include <stddef.h>
#include <stdlib.h>

extern "C" int vm_probe_call(unsigned n,unsigned a,unsigned b) {
    int result;
    asm volatile("int $0x80":"=a"(result):"a"(n),"b"(a),"c"(b):"memory","cc");
    return result;
}
extern "C" [[noreturn]] void vm_probe_panic(unsigned code) {
    static const char failure[]="GTOS V8 PAGE ALLOCATOR FAIL V1\n";
    vm_probe_call(GTOS_SYS_WRITE,(unsigned)failure,sizeof(failure)-1);
    vm_probe_call(GTOS_SYS_EXIT,0x56000000U|code,0);
    asm volatile("ud2");
    __builtin_unreachable();
}
extern "C" [[noreturn]] void abort(void) { vm_probe_panic(0xe0); }
// The leaf uses an automatic PageAllocator and a fixed table. It provides no
// general C++ heap: an unexpected retained allocation or delete terminates.
// Generated deleting virtual destructors retain delete even without a caller.
void* operator new(size_t) { vm_probe_panic(0xe1); }
void* operator new[](size_t) { vm_probe_panic(0xe2); }
void operator delete(void*) noexcept { vm_probe_panic(0xe3); }
void operator delete[](void*) noexcept { vm_probe_panic(0xe4); }
void operator delete(void*,size_t) noexcept { vm_probe_panic(0xe5); }
void operator delete[](void*,size_t) noexcept { vm_probe_panic(0xe6); }
extern "C" [[noreturn]] void __cxa_pure_virtual(void) { vm_probe_panic(0xe7); }
