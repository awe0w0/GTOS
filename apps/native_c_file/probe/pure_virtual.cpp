// Pure virtual dispatch has to stop the probe kernel, not resolve to address zero.
//
// The production kernel defines this symbol in src/kernel.cpp; the probe kernel is its own entry point and the current tree's virtual classes
// (UserMemory, NativeFileEndpoint, BlockDevice) make every vtable name it. The message goes to the debug console the guest evidence is read
// from, so a pure virtual call is visible in the run record instead of looking like a hang.
extern "C" __attribute__((noreturn)) void __cxa_pure_virtual() {
    const char message[] = "C FILE KERNEL FATAL pure_virtual\n";
    for (const char* cursor = message; *cursor; ++cursor)
        __asm__ volatile("outb %0,$0xe9" ::"a"((unsigned char)*cursor));
    for (;;) __asm__ volatile("cli; hlt");
}
