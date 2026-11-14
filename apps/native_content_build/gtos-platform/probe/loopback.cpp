#include "memory_record.h"

#include <process/abi.h>
#include <process/loopback_abi.h>

extern "C" {
__attribute__((section(".data.vm_record"), used))
volatile gtos_perfetto_probe::Record native_memory_record = {1, GTOS_LOOPBACK_GUEST_MODE, 0, 0, 0, 0, 0};
}

namespace {
constexpr unsigned version = GTOS_LOOPBACK_ABI_VERSION;
constexpr unsigned port = 18441;
constexpr unsigned historicalHandle = 1;

template<class Request>
int Call(unsigned op, const Request& request) {
    asm volatile("int $0x80" : "+a"(op)
                 : "b"(&request), "c"(sizeof(request)) : "cc", "memory");
    return static_cast<int>(op);
}

int Control(unsigned op, unsigned handle) {
    return Call(op, GtosLoopbackControlRequest{version, handle});
}

int Option(unsigned op, unsigned handle, unsigned value) {
    return Call(op, GtosLoopbackOptionRequest{version, handle, value});
}

int Transfer(unsigned op, unsigned handle, const void* data, unsigned bytes) {
    return Call(op, GtosLoopbackTransferRequest{
        version, handle, reinterpret_cast<unsigned>(data), bytes});
}

bool Check(bool value) {
    native_memory_record.checks = native_memory_record.checks + 1;
    if (!value) native_memory_record.failures = native_memory_record.failures + 1;
    return value;
}

void Yield() {
    unsigned op = GTOS_SYS_YIELD;
    asm volatile("int $0x80" : "+a"(op) : : "cc", "memory");
}

int Listen() {
    const int handle = Control(GTOS_SYS_LOOPBACK_OPEN, 0);
    if (!Check(handle == static_cast<int>(historicalHandle))) return 1;
    if (!Check(Option(GTOS_SYS_LOOPBACK_BIND, handle, port) == 0) ||
        !Check(Option(GTOS_SYS_LOOPBACK_LISTEN, handle, 2) == 0)) return 2;
    native_memory_record.stage = 1;
    int peer = GTOS_LOOPBACK_ERR_AGAIN;
    for (unsigned wait = 0; wait < 1000 && peer == GTOS_LOOPBACK_ERR_AGAIN; ++wait) {
        Yield();
        peer = Control(GTOS_SYS_LOOPBACK_ACCEPT, handle);
    }
    if (!Check(peer > handle)) return 3;
    const char proof[] = "owned";
    if (!Check(Transfer(GTOS_SYS_LOOPBACK_WRITE, peer, proof, sizeof(proof)) ==
        sizeof(proof))) return 4;
    native_memory_record.stage = 2;
#ifdef GTOS_LOOPBACK_GUEST_WRONG_EXPECTATION
    return 11;
#else
    return 0;
#endif
}

int Connect() {
    if (!Check(Control(GTOS_SYS_LOOPBACK_CLOSE, historicalHandle) == GTOS_LOOPBACK_ERR_BAD_HANDLE) ||
        !Check(Control(GTOS_SYS_LOOPBACK_ACCEPT, historicalHandle) == GTOS_LOOPBACK_ERR_BAD_HANDLE))
        return 5;
    const int handle = Control(GTOS_SYS_LOOPBACK_OPEN, 0);
    if (!Check(handle > static_cast<int>(historicalHandle)) ||
        !Check(Option(GTOS_SYS_LOOPBACK_CONNECT, handle, port) == 0)) return 6;
    char bytes[6] = {};
    int count = GTOS_LOOPBACK_ERR_AGAIN;
    for (unsigned wait = 0; wait < 1000 && count == GTOS_LOOPBACK_ERR_AGAIN; ++wait) {
        Yield();
        count = Transfer(GTOS_SYS_LOOPBACK_READ, handle, bytes, sizeof(bytes));
    }
    if (!Check(count == sizeof(bytes))) return 7;
    const char expected[] = "owned";
    bool equal = true;
    for (unsigned i = 0; i < sizeof(bytes); ++i) equal = equal && bytes[i] == expected[i];
    if (!Check(equal)) return 8;
    count = GTOS_LOOPBACK_ERR_AGAIN;
    for (unsigned wait = 0; wait < 1000 && count == GTOS_LOOPBACK_ERR_AGAIN; ++wait) {
        Yield();
        count = Transfer(GTOS_SYS_LOOPBACK_READ, handle, bytes, sizeof(bytes));
    }
    if (!Check(count == 0) ||
        !Check(Transfer(GTOS_SYS_LOOPBACK_WRITE, handle, bytes, 1) ==
            GTOS_LOOPBACK_ERR_BROKEN_PIPE)) return 9;
    const int revived = Control(GTOS_SYS_LOOPBACK_OPEN, 0);
    if (!Check(revived > handle && Option(GTOS_SYS_LOOPBACK_BIND, revived, port) == 0))
        return 10;
    native_memory_record.stage = 2;
    return 0;
}

}

extern "C" int NativeEntry() {
    return GTOS_LOOPBACK_GUEST_MODE == 0 ? Listen() : Connect();
}
