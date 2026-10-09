#ifndef GTOS_C_CLOCK_SDK_TIME_H
#define GTOS_C_CLOCK_SDK_TIME_H
#include_next <time.h>
#if !defined(__GTOS__)
#error This SDK extension requires the private GTOS C ABI
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 2
#endif
#endif
