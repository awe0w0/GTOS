#ifndef __GTOS__MEMORY__CRITICALSECTION_H
#define __GTOS__MEMORY__CRITICALSECTION_H
#include <common/types.h>
#include <hardwarecommunication/cpu_hal.h>
namespace gtos { namespace memory {
    // Single-CPU allocator protection. Nested calls preserve interrupt state.
    class InterruptGuard {
        hardwarecommunication::cpu::InterruptState state;
        InterruptGuard(const InterruptGuard&);
        InterruptGuard& operator=(const InterruptGuard&);
    public:
        InterruptGuard() : state(hardwarecommunication::cpu::MaskInterrupts()) {}
        ~InterruptGuard() { hardwarecommunication::cpu::RestoreInterrupts(state); }
    };
} }
#endif
