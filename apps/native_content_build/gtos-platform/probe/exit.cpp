#include <process/abi.h>
#include <unistd.h>

#if GTOS_EXIT_GUEST_MODE == 1
#include "absl/debugging/failure_signal_handler.h"
#elif GTOS_EXIT_GUEST_MODE == 2
#include <process/thread_abi.h>
#include <process/vm_abi.h>

namespace {

template<class Request>
int Call(unsigned operation, const Request& request) {
    asm volatile("int $0x80" : "+a"(operation)
                 : "b"(&request), "c"(sizeof(request)) : "memory", "cc");
    return static_cast<int>(operation);
}

[[noreturn]] void FaultingWorker(void*) {
    asm volatile("ud2");
    __builtin_unreachable();
}

int StartFaultingWorker() {
    GtosVmReserveResult stack = {};
    constexpr unsigned page = GTOS_VM_PAGE_BYTES;
    if (Call(GTOS_SYS_VM_RESERVE, GtosVmReserveRequest{GTOS_VM_ABI_VERSION,
            4 * page, page, 0, reinterpret_cast<unsigned>(&stack)}) != 0 ||
        Call(GTOS_SYS_VM_SET_PERMISSIONS, GtosVmRangeRequest{GTOS_VM_ABI_VERSION,
            stack.handle, page, 2 * page, GTOS_VM_READ_WRITE}) != 0)
        return 75;

    unsigned id = 0;
    if (Call(GTOS_SYS_THREAD_CREATE, GtosThreadCreateRequest{GTOS_THREAD_ABI_VERSION,
            0, reinterpret_cast<unsigned>(&FaultingWorker), 0, stack.handle,
            reinterpret_cast<unsigned>(&id)}) != 0 || !id)
        return 76;
    // Either preemption or a yield lets the worker run. The fault must stop
    // the entire group before the parent can report a normal return.
    for (unsigned attempts = 0; attempts < 128; ++attempts) {
        unsigned operation = GTOS_SYS_YIELD;
        asm volatile("int $0x80" : "+a"(operation) : "b"(0), "c"(0) : "memory", "cc");
    }
    return 77;
}

}  // namespace
#endif

extern "C" {
__attribute__((section(".data.native_exit_record")))
volatile unsigned native_exit_record = 1;
}

extern "C" int NativeEntry() {
#if GTOS_EXIT_GUEST_MODE == 1
    const char message[] = "CONTENT SIGNAL CONSUMER READY\n";
#elif GTOS_EXIT_GUEST_MODE == 2
    const char message[] = "CONTENT WORKER CONSUMER READY\n";
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
#elif GTOS_EXIT_GUEST_MODE == 2
    return StartFaultingWorker();
#elif defined(GTOS_EXIT_GUEST_WRONG_EXPECTATION)
    _exit(0x12345679);
#else
    _exit(0x12345678);
#endif
}
