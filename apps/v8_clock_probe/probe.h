#ifndef GTOS_V8_CLOCK_PROBE_H
#define GTOS_V8_CLOCK_PROBE_H
#include "gtos_clock_bridge.h"
#include <process/abi.h>
#include <process/vm_abi.h>
struct NativeClockRecord {
    unsigned version,kind,mode,stage,checks,scenario,error,reserved;
    unsigned long long elapsed_us;
    unsigned legacy_first,legacy_last;
    GtosClockReadResult first,last;
    unsigned first_result,last_result,sentinel_bytes,sentinel_checks;
    unsigned keep_base,keep_handle,keep_pages,sentinel_before;
    unsigned sentinel_after,abi_register_checks,clock_failures,padding;
};
static_assert(sizeof(NativeClockRecord)==192,"Clock guest record wire");
static_assert(__builtin_offsetof(NativeClockRecord,first)==48,"Clock first sample");
static_assert(__builtin_offsetof(NativeClockRecord,last)==96,"Clock last sample");
static_assert(__builtin_offsetof(NativeClockRecord,keep_base)==160,"Clock retained VM");
extern "C" __attribute__((section(".data.clock_record"))) volatile NativeClockRecord native_clock_record;
extern "C" int clock_probe_call(unsigned,unsigned,unsigned);
extern "C" [[noreturn]] void clock_probe_fail(unsigned);
#endif
