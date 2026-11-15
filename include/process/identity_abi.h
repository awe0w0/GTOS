#ifndef GTOS_PROCESS_IDENTITY_ABI_H
#define GTOS_PROCESS_IDENTITY_ABI_H

// i386 int 0x80, EAX=operation. No input addresses or target IDs.
// On success EAX=0, EBX=own process ID, EDX=current thread ID.
// Other GPRs and segments are preserved. Older kernels return -ENOSYS.
#define GTOS_SYS_PROCESS_IDENTITY 0x4718U

#if defined(__cplusplus) && defined(__GTOS__) && defined(__i386__)
#include <process/info_abi.h>

namespace gtos {
namespace native {
struct ProcessIdentity {
    unsigned process_id;
    unsigned thread_id;
};

inline ProcessIdentity CurrentProcessIdentity() {
    ProcessIdentity identity;
    unsigned call = GTOS_SYS_PROCESS_IDENTITY;
    asm volatile("int $0x80"
                 : "+a"(call), "=b"(identity.process_id), "=d"(identity.thread_id)
                 : : "cc");
    if (call == static_cast<unsigned>(GTOS_PROCESS_INFO_ERR_UNSUPPORTED)) {
        GtosProcessInfoResult result = {};
        GtosProcessInfoRequest request = {GTOS_PROCESS_INFO_ABI_VERSION, 0,
            reinterpret_cast<unsigned>(&result), sizeof(result)};
        call = GTOS_SYS_PROCESS_INFO;
        asm volatile("int $0x80" : "+a"(call)
                     : "b"(reinterpret_cast<unsigned>(&request)), "c"(sizeof(request))
                     : "memory", "cc");
        if (result.version != GTOS_PROCESS_INFO_ABI_VERSION) __builtin_trap();
        identity = {result.process_id, result.thread_id};
    }
    if (call || !identity.process_id || !identity.thread_id
        || identity.process_id > 0x7fffffffU || identity.thread_id > 0x7fffffffU)
        __builtin_trap();
    return identity;
}
}
}
#endif

#endif
