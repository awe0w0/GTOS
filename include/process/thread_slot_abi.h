#ifndef GTOS_PROCESS_THREAD_SLOT_ABI_H
#define GTOS_PROCESS_THREAD_SLOT_ABI_H

// Allocation-free TLS indexing for the calling native thread. The slot
// remains stable until exit. A recycled slot always has a different ID.
#define GTOS_SYS_THREAD_SLOT_INFO 0x4769U
// Register-only lookup, i386 int 0x80. No input addresses or thread IDs.
// EAX=0, EBX=slot index, EDX=thread ID. Other GPRs are preserved.
#define GTOS_SYS_THREAD_SLOT_IDENTITY 0x476dU
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

#if defined(__GTOS__) && defined(__i386__)
#include <process/abi.h>

namespace gtos {
namespace native {

inline GtosThreadSlotInfo CurrentThreadSlot() {
    GtosThreadSlotInfo info{GTOS_THREAD_SLOT_ABI_VERSION, 0, 0};
    unsigned call = GTOS_SYS_THREAD_SLOT_IDENTITY;
    asm volatile("int $0x80" : "+a"(call), "=b"(info.index), "=d"(info.thread_id)
                 : : "cc");
    if (call == static_cast<unsigned>(GTOS_ERR_UNSUPPORTED)) {
        info = {};
        const GtosThreadSlotRequest request{GTOS_THREAD_SLOT_ABI_VERSION,
                                          reinterpret_cast<unsigned>(&info)};
        call = GTOS_SYS_THREAD_SLOT_INFO;
        asm volatile("int $0x80" : "+a"(call) : "b"(&request), "c"(sizeof(request))
                     : "memory", "cc");
    }
    if (call || info.version != GTOS_THREAD_SLOT_ABI_VERSION ||
        info.index >= GTOS_CONTENT_THREAD_SLOT_LIMIT || !info.thread_id)
        __builtin_trap();
    return info;
}

}
}
#endif
#endif
#endif
