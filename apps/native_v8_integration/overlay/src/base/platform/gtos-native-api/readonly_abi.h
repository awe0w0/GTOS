#ifndef GTOS_PROCESS_READONLY_ABI_H
#define GTOS_PROCESS_READONLY_ABI_H
// Separately versioned static-data protection extension of native ABI 1.
// EAX=call, EBX=request, ECX=exact wire bytes; EAX=0 or negative error.
// The request is copied completely before mutation. Only the current process's
// non-executable loaded data pages qualify; no PID, physical address or CR3.
#define GTOS_READONLY_ABI_VERSION 1U
#define GTOS_SYS_DATA_READONLY 0x4715U
#define GTOS_READONLY_REQUEST_BYTES 12U
#define GTOS_READONLY_PAGE_BYTES 4096U
#define GTOS_READONLY_ERR_PERMISSION (-13)
#define GTOS_READONLY_ERR_BAD_ADDRESS (-14)
#define GTOS_READONLY_ERR_BAD_SIZE (-22)
#define GTOS_READONLY_ERR_BAD_STATE (-22)
#define GTOS_READONLY_ERR_RANGE (-34)
#define GTOS_READONLY_ERR_UNSUPPORTED_VERSION (-38)

typedef struct GtosDataReadonlyRequest {
    unsigned version, address, length;
} GtosDataReadonlyRequest;

// Positive whole-page ranges only. Tightening is atomic with respect to BSP
// tasks: validate every owned resident page before any PTE changes. Contents,
// frames and mappings remain; repeated calls succeed. No path restores RW.
// Code, stacks, guards, dynamic VM and borrowed kernel mappings are excluded.
// Non-PAE IA32 still has no hardware NX guarantee; this is write protection.
#if defined(__cplusplus)
static_assert(sizeof(unsigned) == 4, "Readonly ABI requires 32-bit unsigned");
static_assert(sizeof(GtosDataReadonlyRequest) == GTOS_READONLY_REQUEST_BYTES, "Readonly request");
#endif
#endif
