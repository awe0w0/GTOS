#ifndef GTOS_PROCESS_VM_ABI_H
#define GTOS_PROCESS_VM_ABI_H
// Separately versioned data-page extension of GTOS i386 native ABI 1.
// int 0x80: EAX=call, EBX=request, ECX=exact wire bytes. EAX=0 or negative
// error; all other general/segment registers preserved. Unsigned virtual
// addresses are returned by checked-copy, never as signed EAX values.
#define GTOS_VM_ABI_VERSION 1U
#define GTOS_SYS_VM_RESERVE 0x470bU
#define GTOS_SYS_VM_SET_PERMISSIONS 0x470cU
#define GTOS_SYS_VM_DECOMMIT 0x470dU
#define GTOS_SYS_VM_RELEASE 0x470eU
#define GTOS_SYS_VM_TRIM 0x470fU
#define GTOS_SYS_VM_DISCARD 0x4710U
#define GTOS_SYS_VM_QUERY 0x4711U
#define GTOS_VM_PAGE_BYTES 4096U
#define GTOS_VM_NONE 0U
#define GTOS_VM_READ 1U
#define GTOS_VM_READ_WRITE 3U
#define GTOS_VM_RESERVE_REQUEST_BYTES 20U
#define GTOS_VM_RESERVE_RESULT_BYTES 20U
#define GTOS_VM_RANGE_REQUEST_BYTES 20U
#define GTOS_VM_CONTROL_REQUEST_BYTES 12U
#define GTOS_VM_QUERY_REQUEST_BYTES 12U
#define GTOS_VM_REGION_INFO_BYTES 20U
#define GTOS_VM_ERR_LIMIT (-7)
#define GTOS_VM_ERR_NO_MEMORY (-12)
#define GTOS_VM_ERR_PERMISSION (-13)
#define GTOS_VM_ERR_BAD_ADDRESS (-14)
#define GTOS_VM_ERR_CONFLICT (-16)
#define GTOS_VM_ERR_BAD_SIZE (-22)
#define GTOS_VM_ERR_BAD_STATE (-22)
#define GTOS_VM_ERR_RANGE (-34)
#define GTOS_VM_ERR_UNSUPPORTED_VERSION (-38)
#define GTOS_VM_ERR_HANDLE_EXHAUSTED (-75)

typedef struct GtosVmReserveRequest {
    unsigned version, length, alignment, hint, result;
} GtosVmReserveRequest;
typedef struct GtosVmReserveResult {
    unsigned version, handle, base, length, page_size;
} GtosVmReserveResult;
typedef struct GtosVmRangeRequest {
    unsigned version, handle, offset, length, protection;
} GtosVmRangeRequest;
typedef struct GtosVmControlRequest {
    unsigned version, handle, length;
} GtosVmControlRequest;
typedef struct GtosVmQueryRequest {
    unsigned version, handle, result;
} GtosVmQueryRequest;
typedef struct GtosVmRegionInfo {
    unsigned version, handle, base, length, resident_pages;
} GtosVmRegionInfo;

// Requests are snapshotted after exact-size checking. The current native
// process owns every operation; no PID, physical address or CR3 is accepted.
// Reserve consumes no frames. Nonzero hint is exact; alignment is a power of
// two >=4096. Lengths are positive page multiples, offsets page aligned.
// R/RW commit holes as new zero pages; NONE retains resident data but removes
// access. Decommit releases pages; later R/RW restores all-zero pages. Discard
// requires a fully resident range and clears bytes, preserving permissions.
// EXEC permissions are refused. Non-PAE IA32 has no hardware NX guarantee.
// Decommit/Discard require protection=0; Release requires length=0. Trim
// retains a positive whole-page prefix no larger than the current reservation.
// Output buffers are completely validated before any reservation mutation.
// Static ELF/stack/guards and borrowed kernel mappings remain frozen.
#if defined(__cplusplus)
static_assert(sizeof(unsigned) == 4, "VM ABI requires 32-bit unsigned");
static_assert(sizeof(GtosVmReserveRequest) == GTOS_VM_RESERVE_REQUEST_BYTES, "VM reserve request");
static_assert(sizeof(GtosVmReserveResult) == GTOS_VM_RESERVE_RESULT_BYTES, "VM reserve result");
static_assert(sizeof(GtosVmRangeRequest) == GTOS_VM_RANGE_REQUEST_BYTES, "VM range request");
static_assert(sizeof(GtosVmControlRequest) == GTOS_VM_CONTROL_REQUEST_BYTES, "VM control request");
static_assert(sizeof(GtosVmQueryRequest) == GTOS_VM_QUERY_REQUEST_BYTES, "VM query request");
static_assert(sizeof(GtosVmRegionInfo) == GTOS_VM_REGION_INFO_BYTES, "VM query result");
#endif
#endif
