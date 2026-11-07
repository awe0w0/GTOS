#ifndef GTOS_PROCESS_SHARED_MEMORY_ABI_H
#define GTOS_PROCESS_SHARED_MEMORY_ABI_H

// IA32 int 0x80. EAX selects the operation; EBX and ECX identify the
// exact-size request. Handles and mappings belong to the calling process.
#define GTOS_SHARED_MEMORY_ABI_VERSION 1U
#define GTOS_SYS_SHARED_MEMORY_CREATE 0x4761U
#define GTOS_SYS_SHARED_MEMORY_DUP 0x4762U
#define GTOS_SYS_SHARED_MEMORY_CLOSE 0x4763U
#define GTOS_SYS_SHARED_MEMORY_CONVERT 0x4764U
#define GTOS_SYS_SHARED_MEMORY_INFO 0x4765U
#define GTOS_SYS_SHARED_MEMORY_MAP 0x4766U
#define GTOS_SYS_SHARED_MEMORY_UNMAP 0x4767U

#define GTOS_SHARED_MEMORY_READ_ONLY 1U
#define GTOS_SHARED_MEMORY_WRITABLE 2U
#define GTOS_SHARED_MEMORY_UNSAFE 3U
#define GTOS_SHARED_MEMORY_PAGE_BYTES 4096U
#define GTOS_SHARED_MEMORY_MAX_BYTES (1024U * GTOS_SHARED_MEMORY_PAGE_BYTES)

#define GTOS_SHARED_MEMORY_ERR_BAD_HANDLE (-9)
#define GTOS_SHARED_MEMORY_ERR_NO_MEMORY (-12)
#define GTOS_SHARED_MEMORY_ERR_PERMISSION (-13)
#define GTOS_SHARED_MEMORY_ERR_BAD_ADDRESS (-14)
#define GTOS_SHARED_MEMORY_ERR_INVALID (-22)
#define GTOS_SHARED_MEMORY_ERR_LIMIT (-24)
#define GTOS_SHARED_MEMORY_ERR_UNAVAILABLE (-38)
#define GTOS_SHARED_MEMORY_ERR_OVERFLOW (-75)

typedef struct GtosSharedMemoryCreateRequest {
    unsigned version, bytes, mode;
} GtosSharedMemoryCreateRequest;
typedef struct GtosSharedMemoryControlRequest {
    unsigned version, handle;
} GtosSharedMemoryControlRequest;
typedef struct GtosSharedMemoryConvertRequest {
    unsigned version, handle, mode;
} GtosSharedMemoryConvertRequest;
typedef struct GtosSharedMemoryInfoRequest {
    unsigned version, handle, result;
} GtosSharedMemoryInfoRequest;
typedef struct GtosSharedMemoryInfo {
    unsigned version, bytes, mode;
} GtosSharedMemoryInfo;
typedef struct GtosSharedMemoryMapRequest {
    unsigned version, handle, offset, bytes, writable, result;
} GtosSharedMemoryMapRequest;
typedef struct GtosSharedMemoryUnmapRequest {
    unsigned version, base, bytes;
} GtosSharedMemoryUnmapRequest;

// CREATE allocates zeroed backing pages and returns a positive handle.
// Writable objects can be converted once, to read-only or unsafe. Conversion
// applies to future mappings; existing writable mappings remain writable.
// DUP returns a new handle for a read-only or unsafe object, in the same
// process. A mapping retains the backing pages after the last handle closes.
// MAP requires a page-aligned offset and a positive byte count within the
// requested size. UNMAP takes the exact returned base and requested count.
// Mapped pages are excluded from the private VM reservation operations.
// At Reap, destroy page tables before releasing shared backing pages.
#if defined(__cplusplus)
static_assert(sizeof(unsigned)==4,"Shared memory ABI unsigned");
static_assert(sizeof(GtosSharedMemoryCreateRequest)==12,"Shared memory create ABI");
static_assert(sizeof(GtosSharedMemoryControlRequest)==8,"Shared memory control ABI");
static_assert(sizeof(GtosSharedMemoryConvertRequest)==12,"Shared memory convert ABI");
static_assert(sizeof(GtosSharedMemoryInfoRequest)==12,"Shared memory info request ABI");
static_assert(sizeof(GtosSharedMemoryInfo)==12,"Shared memory info ABI");
static_assert(sizeof(GtosSharedMemoryMapRequest)==24,"Shared memory map ABI");
static_assert(sizeof(GtosSharedMemoryUnmapRequest)==12,"Shared memory unmap ABI");
#endif
#endif
