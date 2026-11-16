#ifndef GTOS_PROCESS_THREAD_SLOT_ABI_H
#define GTOS_PROCESS_THREAD_SLOT_ABI_H

// Allocation-free TLS indexing for the calling native thread. The slot
// remains stable until exit. A recycled slot always has a different ID.
#define GTOS_SYS_THREAD_SLOT_INFO 0x4769U
#define GTOS_THREAD_SLOT_ABI_VERSION 1U
#define GTOS_THREAD_SLOT_LIMIT 4U
// Trusted Content boot profile, including the browser and in-process renderer.
#define GTOS_CONTENT_THREAD_SLOT_LIMIT 64U
#define GTOS_THREAD_SLOT_ERR_ADDRESS (-14)
#define GTOS_THREAD_SLOT_ERR_SIZE (-22)
#define GTOS_THREAD_SLOT_ERR_VERSION (-38)

typedef struct GtosThreadSlotRequest {
    unsigned version, result;
} GtosThreadSlotRequest;
typedef struct GtosThreadSlotInfo {
    unsigned version, index, thread_id;
} GtosThreadSlotInfo;

#if defined(__cplusplus)
static_assert(sizeof(GtosThreadSlotRequest)==8,"Thread slot request ABI");
static_assert(sizeof(GtosThreadSlotInfo)==12,"Thread slot response ABI");
#endif
#endif
