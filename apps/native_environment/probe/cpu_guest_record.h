#ifndef CPU_GUEST_RECORD_H
#define CPU_GUEST_RECORD_H
#include <process/cpu_clock_abi.h>
struct CpuGuestRecord {
    unsigned version, mode, phase, checks, joined, workerStarted, workerDone;
    unsigned environmentCase, constructorCalls, environmentPointer;
    unsigned ids[3], initial[3], final[3];
    GtosCpuClockReadResult beforeJoin, afterJoin, afterReuse;
};
#define CPU_RECORD_VA 0x40020000U
#endif
