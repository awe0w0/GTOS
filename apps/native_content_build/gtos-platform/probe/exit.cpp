#include <process/abi.h>
#include <unistd.h>

extern "C" {
__attribute__((section(".data.native_exit_record")))
volatile unsigned native_exit_record = 1;
}

extern "C" int NativeEntry() {
    const char message[] = "CONTENT EXIT CONSUMER READY\n";
    unsigned operation = GTOS_SYS_WRITE;
    asm volatile("int $0x80" : "+a"(operation)
                 : "b"(message), "c"(sizeof(message) - 1) : "memory", "cc");
    if (operation != sizeof(message) - 1)
        return 72;
    native_exit_record = 2;
#if defined(GTOS_EXIT_GUEST_WRONG_EXPECTATION)
    _exit(0x12345679);
#else
    _exit(0x12345678);
#endif
}
