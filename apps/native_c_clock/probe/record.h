#ifndef GTOS_NATIVE_C_CLOCK_RECORD_H
#define GTOS_NATIVE_C_CLOCK_RECORD_H
struct ClockSample {
    unsigned kind, status;
    long long seconds, nanoseconds;
    unsigned error, reserved;
};
struct ClockRecord {
    unsigned version, mode, stage, error, checks, rejected, base, handle;
    unsigned count, pending, errno_va, errno_value, heap_base, heap_handle, heap_bytes, sentinel_checks;
    ClockSample samples[6];
};
static_assert(sizeof(ClockSample) == 32 && sizeof(ClockRecord) == 256, "Serialized C clock record");
#define GTOS_C_CLOCK_RECORD_VA 0x40020000U
#endif
