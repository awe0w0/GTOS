#ifndef GTOS_CONTENT_UNISTD_H
#define GTOS_CONTENT_UNISTD_H

#if !defined(__GTOS__) || !defined(__i386__) || defined(__linux__) || defined(_WIN32)
#error This header requires the native GTOS IA32 ABI
#endif

#include_next <unistd.h>
#include <process/abi.h>

// The private SDK's immediate process exit preserves the complete status.
#ifdef __cplusplus
extern "C" {
[[noreturn]] static inline void _exit(int status) noexcept {
#else
_Noreturn static inline void _exit(int status) {
#endif
    unsigned operation = GTOS_SYS_EXIT;
    __asm__ volatile("int $0x80" : "+a"(operation)
                 : "b"((unsigned)status), "c"(0u) : "memory", "cc");
    __builtin_trap();
}
#ifdef __cplusplus
}
#endif

#endif  // GTOS_CONTENT_UNISTD_H
