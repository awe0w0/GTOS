#ifndef GTOS_PROCESS_REALTIME_ABI_H
#define GTOS_PROCESS_REALTIME_ABI_H
// Independent read-only UTC extension; the monotonic clock ABI is unchanged.
// int 0x80: EAX=call, EBX=request VA, ECX=exact bytes. EAX=0 or negative error.
// Epoch is a validated PC CMOS UTC calendar sampled once at activation, plus
// delivered BSP PIT elapsed time. No sub-second RTC phase or missed IRQ recovery.
#define GTOS_REALTIME_ABI_VERSION 1U
#define GTOS_SYS_REALTIME_READ 0x4714U
#define GTOS_REALTIME_READ_REQUEST_BYTES 16U
#define GTOS_REALTIME_READ_RESULT_BYTES 48U
#define GTOS_REALTIME_UNIT_MICROSECONDS 1U
#define GTOS_REALTIME_SOURCE_CMOS_PIT 1U
#define GTOS_REALTIME_CAP_RTC_VALIDATED 1U
#define GTOS_REALTIME_CAP_UTC 2U
#define GTOS_REALTIME_CAP_COARSE 4U
#define GTOS_REALTIME_CAP_IRQ_ACCOUNTED 8U
#define GTOS_REALTIME_REQUIRED_CAPABILITIES 15U
#define GTOS_REALTIME_RESOLUTION_US 10000U
#define GTOS_REALTIME_ANCHOR_UNCERTAINTY_US 1000000U
#define GTOS_REALTIME_ERR_BAD_SIZE (-22)
#define GTOS_REALTIME_ERR_BAD_STATE (-22)
#define GTOS_REALTIME_ERR_BAD_ADDRESS (-14)
#define GTOS_REALTIME_ERR_UNSUPPORTED (-38)
#define GTOS_REALTIME_ERR_OVERFLOW (-75)
typedef struct GtosRealtimeReadRequest {
    unsigned version, flags, result_va, result_bytes;
} GtosRealtimeReadRequest;
typedef struct GtosRealtimeReadResult {
    unsigned version, unit, source, capabilities, resolution_us, anchor_uncertainty_us;
    unsigned long long microseconds, monotonic_microseconds, delivered_ticks;
} GtosRealtimeReadResult;
// Error order: wire size, whole request snapshot, version, flags/result size,
// whole output prevalidation, coherent clock snapshot, checked output copy.
// Errors leave output unchanged; request/output overlap is supported. A missing,
// stopped, invalid or unsupported CMOS RTC returns UNSUPPORTED, never uptime.
// The PC profile treats CMOS as UTC, requires century register 0x32 and a running
// 32kHz divider, and rejects RTC SET/DST modes. No syscall can set or reseed it.
#if defined(__cplusplus)
static_assert(sizeof(unsigned) == 4, "Realtime 32-bit wire words");
static_assert(sizeof(unsigned long long) == 8, "Realtime 64-bit time");
static_assert(sizeof(GtosRealtimeReadRequest) == GTOS_REALTIME_READ_REQUEST_BYTES, "Realtime request");
static_assert(sizeof(GtosRealtimeReadResult) == GTOS_REALTIME_READ_RESULT_BYTES, "Realtime result");
static_assert(__builtin_offsetof(GtosRealtimeReadResult, microseconds) == 24, "Realtime epoch offset");
static_assert(__builtin_offsetof(GtosRealtimeReadResult, monotonic_microseconds) == 32, "Realtime monotonic offset");
static_assert(__builtin_offsetof(GtosRealtimeReadResult, delivered_ticks) == 40, "Realtime ticks offset");
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(GtosRealtimeReadRequest) == GTOS_REALTIME_READ_REQUEST_BYTES, "Realtime request");
_Static_assert(sizeof(GtosRealtimeReadResult) == GTOS_REALTIME_READ_RESULT_BYTES, "Realtime result");
#endif
#endif
