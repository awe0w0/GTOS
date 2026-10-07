#include <process/abi.h>
#include <stdlib.h>
#include "heap.h"
extern "C" int heap_probe_call(unsigned operation,unsigned first,unsigned second) {
    int result;
    asm volatile("int $0x80":"=a"(result):"a"(operation),"b"(first),"c"(second):"memory","cc");
    return result;
}
extern "C" [[noreturn]] void abort(void) { gtos_native_heap_panic(0x21U); }
