#ifndef GTOS_CONTENT_SYS_TIME_H
#define GTOS_CONTENT_SYS_TIME_H

#if !defined(__GTOS__) || !defined(__i386__) || defined(__linux__) || defined(_WIN32)
#error This header requires the native GTOS IA32 ABI
#endif

#include <llvm-libc-types/struct_timeval.h>

#endif  // GTOS_CONTENT_SYS_TIME_H
