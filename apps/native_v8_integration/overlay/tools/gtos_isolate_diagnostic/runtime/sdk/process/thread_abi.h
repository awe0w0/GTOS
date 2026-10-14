#ifndef GTOS_PROCESS_THREAD_ABI_H
#define GTOS_PROCESS_THREAD_ABI_H
// Opt-in BSP shared-address-space lifecycle; not a POSIX or TLS ABI.
// int 0x80: EAX=call, EBX=request, ECX=exact wire bytes. Other GPRs
// and segment registers are preserved. EXIT takes EBX=unsigned exit code.
#define GTOS_THREAD_ABI_VERSION 1U
#define GTOS_SYS_THREAD_CREATE 0x4730U
#define GTOS_SYS_THREAD_TRY_JOIN 0x4731U
#define GTOS_SYS_THREAD_EXIT 0x4732U
#define GTOS_THREAD_CREATE_REQUEST_BYTES 24U
#define GTOS_THREAD_JOIN_REQUEST_BYTES 12U
#define GTOS_THREAD_ERR_LIMIT (-11)
#define GTOS_THREAD_ERR_BAD_ADDRESS (-14)
#define GTOS_THREAD_ERR_BUSY (-16)
#define GTOS_THREAD_ERR_BAD_SIZE (-22)
#define GTOS_THREAD_ERR_BAD_STATE (-22)
#define GTOS_THREAD_ERR_DEADLOCK (-35)
#define GTOS_THREAD_ERR_UNSUPPORTED (-38)
#define GTOS_THREAD_ERR_NO_THREAD (-3)
typedef struct GtosThreadCreateRequest {
    unsigned version, flags, entry, argument, stack_handle, result_va;
} GtosThreadCreateRequest;
typedef struct GtosThreadJoinRequest {
    unsigned version, thread_id, result_va;
} GtosThreadJoinRequest;
// Create requires a private reservation: guard page, >=2 writable stack
// pages, guard page. The whole reservation remains pinned until successful
// TRY_JOIN or group termination. A worker starts at entry(argument) with
// cdecl ESP+4 aligned to 16; entry must call THREAD_EXIT and never return.
// Entry must be inside the admitted image's executable segment, not heap.
// Success copies a positive ID. Failure leaves output/stack bytes unchanged.
// TRY_JOIN returns BUSY for a live worker. A user Join wrapper may Yield
// and retry; completion copies the exit code and consumes the record once.
// Self join, main-task join and cross-process join are refused. No detach.
// Process EXIT/fault kills every same-process task before deferred reap;
// worker EXIT alone keeps the address space, FILE and windows alive.
// MaximumProcesses bounds the aggregate number of native contexts, not
// four workers per process. There is one scheduler CPU, no SMP execution.
#if defined(__cplusplus)
static_assert(sizeof(unsigned) == 4, "Thread ABI requires 32-bit unsigned");
static_assert(sizeof(GtosThreadCreateRequest) == GTOS_THREAD_CREATE_REQUEST_BYTES, "Thread create size");
static_assert(sizeof(GtosThreadJoinRequest) == GTOS_THREAD_JOIN_REQUEST_BYTES, "Thread join size");
#endif
#endif
