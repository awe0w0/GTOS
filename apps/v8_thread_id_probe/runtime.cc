#include "probe.h"
#include <stdlib.h>
extern "C" int tls_probe_call(unsigned operation, unsigned first, unsigned second) {
    int result;
    asm volatile("int $0x80" : "=a"(result) : "a"(operation), "b"(first), "c"(second) : "memory", "cc");
    return result;
}
extern "C" [[noreturn]] void tls_probe_fail(unsigned code) {
    native_tls_record.error = code;
    tls_probe_call(GTOS_SYS_EXIT, 0x4A010000U | code, 0);
    asm volatile("ud2");
    __builtin_unreachable();
}
extern "C" [[noreturn]] void abort(void) { tls_probe_fail(255); }
