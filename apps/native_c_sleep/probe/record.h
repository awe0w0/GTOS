#ifndef GTOS_NATIVE_SLEEP_RECORD_H
#define GTOS_NATIVE_SLEEP_RECORD_H
struct SleepSample {
    unsigned kind, status;
    long long requested_seconds, requested_nanoseconds;
    long long before_seconds, before_nanoseconds, after_seconds, after_nanoseconds;
};
struct SleepRecord {
    unsigned version, mode, stage, error, checks, count, base, handle;
    unsigned errno_va, errno_value, heap_base, heap_bytes;
    unsigned active, marker, wait_va, rejected;
    SleepSample samples[12];
};
#define GTOS_SLEEP_RECORD_VA 0x40040000U
static_assert(sizeof(SleepSample) == 56 && sizeof(SleepRecord) == 736, "Serialized sleep record");
#endif
