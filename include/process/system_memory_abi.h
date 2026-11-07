#ifndef GTOS_PROCESS_SYSTEM_MEMORY_ABI_H
#define GTOS_PROCESS_SYSTEM_MEMORY_ABI_H

// IA32 int 0x80, versioned aggregate snapshot of the physical allocator.
#define GTOS_SYS_SYSTEM_MEMORY_INFO 0x4768U
#define GTOS_SYSTEM_MEMORY_ABI_VERSION 1U
#define GTOS_SYSTEM_MEMORY_ERR_ADDRESS (-14)
#define GTOS_SYSTEM_MEMORY_ERR_SIZE (-22)
#define GTOS_SYSTEM_MEMORY_ERR_VERSION (-38)

typedef struct GtosSystemMemoryRequest {
    unsigned version, result;
} GtosSystemMemoryRequest;
typedef struct GtosSystemMemoryInfo {
    unsigned version, page_bytes, total_frames, free_frames;
} GtosSystemMemoryInfo;

// Usable frames exclude firmware, holes, boot reservations and other
// permanently reserved memory. Free frames are immediately allocatable.
// No addresses or other owners' private data are disclosed.
#if defined(__cplusplus)
static_assert(sizeof(GtosSystemMemoryRequest)==8,"System memory request ABI");
static_assert(sizeof(GtosSystemMemoryInfo)==16,"System memory response ABI");
#endif
#endif
