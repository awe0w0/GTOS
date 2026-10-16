#ifndef GTOS_NATIVE_UNISTD_H
#define GTOS_NATIVE_UNISTD_H
#include "__llvm-libc-common.h"
#if !defined(__GTOS__) || !defined(__i386__) || defined(__linux__)
#error GTOS descriptor declarations require the native IA32 ABI
#endif
// Descriptors are owned positive native handles, including file fileno results.
// No stdio descriptor numbers or additional POSIX services are synthesized.
__BEGIN_C_DECLS
int dup(int) __NOEXCEPT;
int close(int) __NOEXCEPT;
__END_C_DECLS
#endif
