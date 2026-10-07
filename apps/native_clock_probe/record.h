#ifndef GTOS_NATIVE_CLOCK_PROBE_RECORD_H
#define GTOS_NATIVE_CLOCK_PROBE_RECORD_H
#include <process/clock_abi.h>
struct ClockProbeRecord {
    unsigned version, kind, mode, stage, checks, scenario, error, reserved;
    unsigned long long elapsed_us;
    unsigned legacy_first, legacy_last;
    GtosClockReadResult first, last;
    unsigned first_result, last_result, sentinel_bytes, sentinel_checks;
    unsigned keep_base, keep_handle, keep_pages, sentinel_before;
    unsigned sentinel_after, abi_register_checks, clock_failures, padding;
};
static_assert(sizeof(ClockProbeRecord) == 192, "Clock probe record size");
static_assert(__builtin_offsetof(ClockProbeRecord, elapsed_us) == 32, "Clock elapsed field");
static_assert(__builtin_offsetof(ClockProbeRecord, first) == 48, "Clock first payload");
static_assert(__builtin_offsetof(ClockProbeRecord, last) == 96, "Clock last payload");
static_assert(__builtin_offsetof(ClockProbeRecord, keep_base) == 160, "Clock retained VM fields");
#define GTOS_CLOCK_PROBE_RECORD_VA 0x40020000U
#define GTOS_CLOCK_PROBE_GUARD_VA 0xBFFFCFFCU
#define GTOS_CLOCK_PROBE_FATAL 0x49000001U
#endif
