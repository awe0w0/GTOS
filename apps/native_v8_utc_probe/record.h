#ifndef GTOS_NATIVE_V8_UTC_RECORD_H
#define GTOS_NATIVE_V8_UTC_RECORD_H
#include "src/base/platform/gtos-native-api/realtime_abi.h"
struct UtcSample {
    unsigned kind, reserved;
    unsigned long long value;
    double js_ms;
};
struct UtcRecord {
    unsigned version, mode, stage, error, checks, rejected, base, handle;
    unsigned sample_count, pending_kind;
    GtosRealtimeReadResult raw;
    UtcSample samples[8];
    unsigned keep_pages, sentinel_checks;
};
static_assert(sizeof(UtcSample) == 24, "UTC actual method sample");
static_assert(sizeof(UtcRecord) == 288, "UTC serialized diagnostic record");
static_assert(__builtin_offsetof(UtcRecord, raw) == 40, "UTC raw offset");
static_assert(__builtin_offsetof(UtcRecord, samples) == 88, "UTC samples offset");
#define GTOS_UTC_RECORD_VA 0x40020000U
#endif
