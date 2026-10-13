#ifndef GTOS_PROCESS_INFO_ABI_H
#define GTOS_PROCESS_INFO_ABI_H
// Separately versioned i386 self query. EAX=call, EBX=unsigned request VA,
// ECX=exact bytes; EAX=0 or negative error. Other GPRs/segments are preserved.
#define GTOS_PROCESS_INFO_ABI_VERSION 1U
#define GTOS_SYS_PROCESS_INFO 0x4713U
#define GTOS_PROCESS_INFO_REQUEST_BYTES 16U
#define GTOS_PROCESS_INFO_RESULT_BYTES 48U
#define GTOS_PROCESS_INFO_ERR_BAD_SIZE (-22)
#define GTOS_PROCESS_INFO_ERR_BAD_ADDRESS (-14)
#define GTOS_PROCESS_INFO_ERR_UNSUPPORTED (-38)

typedef struct GtosProcessInfoRequest {
    unsigned version, flags, result_va, result_bytes;
} GtosProcessInfoRequest;
typedef struct GtosProcessInfoResult {
    unsigned version, process_id, thread_id, stack_begin, stack_end;
    unsigned user_begin, user_end, page_bytes, maximum_pages, maximum_regions;
    unsigned maximum_parallel_threads, scheduler_cpu_count;
} GtosProcessInfoResult;

// IDs identify only the current native task. A process has one task in its
// private address space, hence thread_id == process_id. IDs are positive,
// stable until Reap and never reused; uint32 IDs need a checked signed cast.
// Stack/user ends are exclusive. Stack bounds exclude both guard pages.
// Maximum pages includes static loads, stack and committed dynamic pages;
// it does not count page tables, directory or retained kernel stacks.
// Regions are reservations, not resident pages. Query changes none of them.
// The BSP-only scheduler supplies one CPU and no shared-address-space threads.
// These limits describe the existing runtime, not physical RAM/free capacity.
//
// Error order: ECX size, whole request copy, version, flags/result size,
// whole writable output validation, current-task snapshot and checked copy.
// No target ID is accepted. Output may overlap the request, including partial
// overlap and unaligned addresses. Failure leaves every output byte unchanged.
// No allocation, yield, timer update, CR3 exposure or kernel pointer is involved.
#if defined(__cplusplus)
static_assert(sizeof(unsigned) == 4, "Process info ABI requires 32-bit unsigned");
static_assert(sizeof(GtosProcessInfoRequest) == GTOS_PROCESS_INFO_REQUEST_BYTES, "Process info request size");
static_assert(__builtin_offsetof(GtosProcessInfoRequest, version) == 0, "Process info request version");
static_assert(__builtin_offsetof(GtosProcessInfoRequest, flags) == 4, "Process info request flags");
static_assert(__builtin_offsetof(GtosProcessInfoRequest, result_va) == 8, "Process info request output");
static_assert(__builtin_offsetof(GtosProcessInfoRequest, result_bytes) == 12, "Process info request output size");
static_assert(sizeof(GtosProcessInfoResult) == GTOS_PROCESS_INFO_RESULT_BYTES, "Process info result size");
static_assert(__builtin_offsetof(GtosProcessInfoResult, version) == 0, "Process info version");
static_assert(__builtin_offsetof(GtosProcessInfoResult, process_id) == 4, "Process info process ID");
static_assert(__builtin_offsetof(GtosProcessInfoResult, thread_id) == 8, "Process info thread ID");
static_assert(__builtin_offsetof(GtosProcessInfoResult, stack_begin) == 12, "Process info stack begin");
static_assert(__builtin_offsetof(GtosProcessInfoResult, stack_end) == 16, "Process info stack end");
static_assert(__builtin_offsetof(GtosProcessInfoResult, user_begin) == 20, "Process info user begin");
static_assert(__builtin_offsetof(GtosProcessInfoResult, user_end) == 24, "Process info user end");
static_assert(__builtin_offsetof(GtosProcessInfoResult, page_bytes) == 28, "Process info page bytes");
static_assert(__builtin_offsetof(GtosProcessInfoResult, maximum_pages) == 32, "Process info page limit");
static_assert(__builtin_offsetof(GtosProcessInfoResult, maximum_regions) == 36, "Process info region limit");
static_assert(__builtin_offsetof(GtosProcessInfoResult, maximum_parallel_threads) == 40, "Process info thread limit");
static_assert(__builtin_offsetof(GtosProcessInfoResult, scheduler_cpu_count) == 44, "Process info CPU count");
#endif
#endif
