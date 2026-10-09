#ifndef GTOS_C_CLOCK_SDK_ERRNO_H
#define GTOS_C_CLOCK_SDK_ERRNO_H
#include_next <errno.h>
#if !defined(__GTOS__)
#error This SDK extension requires the private GTOS C ABI
#endif
#ifndef ENOSYS
#define ENOSYS 9942
#endif
#ifndef EOVERFLOW
#define EOVERFLOW 9940
#endif
#endif
