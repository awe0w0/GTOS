#ifndef __GTOS__MEMORY__CRITICALSECTION_H
#define __GTOS__MEMORY__CRITICALSECTION_H
#include <common/types.h>
namespace gtos { namespace memory {
    // Single-CPU allocator protection. Not an SMP lock. Nested calls preserve IF.
    class InterruptGuard {
        uint32_t flags;
        InterruptGuard(const InterruptGuard&);
        InterruptGuard& operator=(const InterruptGuard&);
    public:
        InterruptGuard() : flags(0) {
#ifndef GTOS_MEMORY_TEST
            asm volatile("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
#endif
        }
        ~InterruptGuard() {
#ifndef GTOS_MEMORY_TEST
            if (flags & (1u << 9)) asm volatile("sti" : : : "memory");
#endif
        }
    };
} }
#endif
