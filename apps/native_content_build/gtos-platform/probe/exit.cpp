#include <process/abi.h>
#include <unistd.h>

#if GTOS_EXIT_GUEST_MODE == 1
#include "absl/debugging/failure_signal_handler.h"
#endif

extern "C" {
__attribute__((section(".data.native_exit_record")))
volatile unsigned native_exit_record = 1;
}

extern "C" int NativeEntry() {
#if GTOS_EXIT_GUEST_MODE == 1
    const char message[] = "CONTENT SIGNAL CONSUMER READY\n";
#else
    const char message[] = "CONTENT EXIT CONSUMER READY\n";
#endif
    unsigned operation = GTOS_SYS_WRITE;
    asm volatile("int $0x80" : "+a"(operation)
                 : "b"(message), "c"(sizeof(message) - 1) : "memory", "cc");
    if (operation != sizeof(message) - 1)
        return 72;
    native_exit_record = 2;
#if GTOS_EXIT_GUEST_MODE == 1
    absl::InstallFailureSignalHandler({});
    return 73;
#elif defined(GTOS_EXIT_GUEST_WRONG_EXPECTATION)
    _exit(0x12345679);
#else
    _exit(0x12345678);
#endif
}
