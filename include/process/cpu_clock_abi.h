#ifndef GTOS_PROCESS_CPU_CLOCK_ABI_H
#define GTOS_PROCESS_CPU_CLOCK_ABI_H
// Self-only i386 CPU accounting, separate from uptime and UTC. EAX=call,
// EBX=request VA, ECX=exact bytes; EAX=0 or error, other GPRs preserved.
#define GTOS_CPU_CLOCK_ABI_VERSION 1U
#define GTOS_SYS_CPU_CLOCK_READ 0x4716U
#define GTOS_CPU_CLOCK_REQUEST_BYTES 20U
#define GTOS_CPU_CLOCK_RESULT_BYTES 48U
#define GTOS_CPU_CLOCK_THREAD 1U
#define GTOS_CPU_CLOCK_PROCESS 2U
#define GTOS_CPU_CLOCK_UNIT_MICROSECONDS 1U
#define GTOS_CPU_CLOCK_SOURCE_PIT_RUNNING_SAMPLE 1U
#define GTOS_CPU_CLOCK_CAP_MONOTONIC 1U
#define GTOS_CPU_CLOCK_CAP_COARSE 2U
#define GTOS_CPU_CLOCK_CAP_SELF_ONLY 4U
#define GTOS_CPU_CLOCK_REQUIRED_CAPABILITIES 7U
#define GTOS_CPU_CLOCK_ERR_BAD_SIZE (-22)
#define GTOS_CPU_CLOCK_ERR_BAD_STATE (-22)
#define GTOS_CPU_CLOCK_ERR_BAD_ADDRESS (-14)
#define GTOS_CPU_CLOCK_ERR_UNSUPPORTED (-38)
#define GTOS_CPU_CLOCK_ERR_OVERFLOW (-75)

typedef struct GtosCpuClockReadRequest {
    unsigned version, scope, flags, result_va, result_bytes;
} GtosCpuClockReadRequest;
typedef struct GtosCpuClockReadResult {
    unsigned version, scope, unit, source, capabilities, resolution_us;
    unsigned long long microseconds, charged_ticks;
    unsigned pit_input_hz, pit_divisor;
} GtosCpuClockReadResult;

// Counts delivered BSP PIT IRQs sampled while this native thread is Running;
// process scope includes all its threads, including joined/reaped workers.
// Boot/kernel tasks, sleeping tasks and timer-independent reschedules add zero.
// Resolution is coarse: no sub-tick interpolation or missed-IRQ compensation.
// Ordered validation: size, complete request snapshot, version, flags/output
// size, scope, whole output range, guarded current-task snapshot, checked copy.
// Request/output overlap is allowed; errors leave output unchanged. Reads do
// not allocate, schedule, change counters or expose another process's state.
// Overflow is sticky until this thread/process lifetime ends, with no wrap.
#if defined(__cplusplus)
static_assert(sizeof(unsigned) == 4 && sizeof(unsigned long long) == 8, "CPU clock word sizes");
static_assert(sizeof(GtosCpuClockReadRequest) == 20, "CPU clock request size");
static_assert(__builtin_offsetof(GtosCpuClockReadRequest, result_va) == 12, "CPU clock output VA");
static_assert(__builtin_offsetof(GtosCpuClockReadRequest, result_bytes) == 16, "CPU clock output size");
static_assert(sizeof(GtosCpuClockReadResult) == 48, "CPU clock result size");
static_assert(__builtin_offsetof(GtosCpuClockReadResult, microseconds) == 24, "CPU clock time offset");
static_assert(__builtin_offsetof(GtosCpuClockReadResult, charged_ticks) == 32, "CPU clock tick offset");
static_assert(__builtin_offsetof(GtosCpuClockReadResult, pit_input_hz) == 40, "CPU clock PIT offset");
#endif
#endif
