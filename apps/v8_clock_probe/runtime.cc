#include "probe.h"
#include <stdlib.h>
extern "C" int clock_probe_call(unsigned operation,unsigned first,unsigned second) {
    int result;
    asm volatile("int $0x80":"=a"(result):"a"(operation),"b"(first),"c"(second):"memory","cc");
    return result;
}
extern "C" [[noreturn]] void clock_probe_fail(unsigned code) {
    native_clock_record.error=code;
    clock_probe_call(GTOS_SYS_EXIT,code,0);
    asm volatile("ud2");
    __builtin_unreachable();
}
extern "C" [[noreturn]] void abort(void) { clock_probe_fail(0x49000002U); }
