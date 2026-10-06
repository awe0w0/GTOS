#ifndef GTOS_NATIVE_PROBE_H
#define GTOS_NATIVE_PROBE_H
#include <common/types.h>
#include <process/abi.h>
struct ProbeData {
    uint32_t signature, progress, errors;
};
static inline __attribute__((always_inline)) uint32_t Call(uint32_t operation, uint32_t first = 0,
                                                           uint32_t second = 0) {
    uint32_t result;
    asm volatile("int $0x80"
                 : "=a"(result)
                 : "a"(operation), "b"(first), "c"(second)
                 : "memory", "cc");
    return result;
}
static inline __attribute__((always_inline, noreturn)) void Exit(uint32_t code) {
    Call(GTOS_SYS_EXIT, code);
    for (;;)
        asm volatile("ud2");
}
#endif
