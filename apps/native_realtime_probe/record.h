#ifndef GTOS_NATIVE_REALTIME_PROBE_RECORD_H
#define GTOS_NATIVE_REALTIME_PROBE_RECORD_H
#include <process/realtime_abi.h>
#include <process/clock_abi.h>
struct RealtimeProbeRecord {
    unsigned version, mode, stage, checks, error, rejected, abi, reserved;
    GtosRealtimeReadResult first, last;
    GtosClockReadResult monoFirst, monoLast;
    unsigned base, handle, sentinels, padding[5];
};
static_assert(sizeof(RealtimeProbeRecord) == 256, "Realtime diagnostic record");
#define GTOS_REALTIME_RECORD_VA 0x40020000U
#endif
