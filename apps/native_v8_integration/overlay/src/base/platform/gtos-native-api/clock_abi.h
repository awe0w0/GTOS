#ifndef GTOS_PROCESS_CLOCK_ABI_H
#define GTOS_PROCESS_CLOCK_ABI_H
// Separately versioned i386 coarse monotonic clock. EAX=call, EBX=unsigned
// request VA, ECX=exact bytes; EAX=0 or negative error, other GPRs preserved.
#define GTOS_CLOCK_ABI_VERSION 1U
#define GTOS_SYS_CLOCK_READ 0x4712U
#define GTOS_CLOCK_READ_REQUEST_BYTES 20U
#define GTOS_CLOCK_READ_RESULT_BYTES 48U
#define GTOS_CLOCK_ID_MONOTONIC 1U
#define GTOS_CLOCK_ID_REALTIME 2U
#define GTOS_CLOCK_ID_THREAD_CPU 3U
#define GTOS_CLOCK_UNIT_MICROSECONDS 1U
#define GTOS_CLOCK_SOURCE_PIT_DELIVERED_IRQ 1U
#define GTOS_CLOCK_CAP_MONOTONIC 1U
#define GTOS_CLOCK_CAP_COARSE 2U
#define GTOS_CLOCK_CAP_IRQ_ACCOUNTED 4U
#define GTOS_CLOCK_REQUIRED_CAPABILITIES 7U
#define GTOS_CLOCK_RESOLUTION_US 10000U
// Shared with the actual PIT programming; this is the nominal oscillator input.
#define GTOS_CLOCK_PIT_INPUT_HZ 1193182U
#define GTOS_CLOCK_PIT_DIVISOR 11931U
#define GTOS_CLOCK_ERR_BAD_SIZE (-22)
#define GTOS_CLOCK_ERR_BAD_STATE (-22)
#define GTOS_CLOCK_ERR_BAD_ADDRESS (-14)
#define GTOS_CLOCK_ERR_UNSUPPORTED (-38)
#define GTOS_CLOCK_ERR_OVERFLOW (-75)

typedef struct GtosClockReadRequest {
    unsigned version, clock_id, flags, result_va, result_bytes;
} GtosClockReadRequest;
typedef struct GtosClockReadResult {
    unsigned version, clock_id, unit, source, capabilities, resolution_us;
    unsigned long long microseconds, delivered_ticks;
    unsigned pit_input_hz, pit_divisor;
} GtosClockReadResult;

// Error order: ECX size, whole request snapshot, version, flags/result size,
// clock ID, whole output prevalidation, guarded clock snapshot, checked copy.
// Only MONOTONIC is supported. No request supplies a seed or changes the clock.
// Output may overlap any request bytes: every request word is consumed first.
// Failures leave the output unchanged. Read allocates no frame, region or heap.
// Uptime accounts only delivered BSP IRQs, with no sub-IRQ interpolation or
// recovery of missed IRQs. It is coarse, not epoch time or thread CPU time.
// UINT64 exhaustion is sticky and preserves the last complete internal tuple;
// all subsequent reads fail OVERFLOW. Legacy 32-bit ticks still wrap normally.
#if defined(__cplusplus)
static_assert(sizeof(unsigned) == 4, "Clock ABI requires 32-bit unsigned");
static_assert(sizeof(unsigned long long) == 8, "Clock ABI requires unsigned 64-bit time");
static_assert(sizeof(GtosClockReadRequest) == GTOS_CLOCK_READ_REQUEST_BYTES, "Clock request size");
static_assert(__builtin_offsetof(GtosClockReadRequest, version) == 0, "Clock request version");
static_assert(__builtin_offsetof(GtosClockReadRequest, clock_id) == 4, "Clock request ID");
static_assert(__builtin_offsetof(GtosClockReadRequest, flags) == 8, "Clock request flags");
static_assert(__builtin_offsetof(GtosClockReadRequest, result_va) == 12, "Clock request output");
static_assert(__builtin_offsetof(GtosClockReadRequest, result_bytes) == 16, "Clock request output size");
static_assert(sizeof(GtosClockReadResult) == GTOS_CLOCK_READ_RESULT_BYTES, "Clock result size");
static_assert(__builtin_offsetof(GtosClockReadResult, version) == 0, "Clock result version");
static_assert(__builtin_offsetof(GtosClockReadResult, clock_id) == 4, "Clock result ID");
static_assert(__builtin_offsetof(GtosClockReadResult, unit) == 8, "Clock result unit");
static_assert(__builtin_offsetof(GtosClockReadResult, source) == 12, "Clock result source");
static_assert(__builtin_offsetof(GtosClockReadResult, capabilities) == 16, "Clock result capabilities");
static_assert(__builtin_offsetof(GtosClockReadResult, resolution_us) == 20, "Clock result resolution");
static_assert(__builtin_offsetof(GtosClockReadResult, microseconds) == 24, "Clock result time");
static_assert(__builtin_offsetof(GtosClockReadResult, delivered_ticks) == 32, "Clock result ticks");
static_assert(__builtin_offsetof(GtosClockReadResult, pit_input_hz) == 40, "Clock result input");
static_assert(__builtin_offsetof(GtosClockReadResult, pit_divisor) == 44, "Clock result divisor");
#endif
#endif