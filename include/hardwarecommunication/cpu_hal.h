#ifndef GTOS_HARDWARECOMMUNICATION_CPU_HAL_H
#define GTOS_HARDWARECOMMUNICATION_CPU_HAL_H

namespace gtos { namespace hardwarecommunication { namespace cpu {

    // Local interrupt state. These operations do not synchronize different CPUs.
    enum InterruptState { InterruptsMasked, InterruptsUnmasked };

    inline InterruptState MaskInterrupts() {
#if defined(GTOS_MEMORY_TEST) || defined(GTOS_CPU_TEST)
        return InterruptsUnmasked;
#elif defined(__i386__)
        unsigned flags;
        asm volatile("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
        return (flags & (1U << 9)) ? InterruptsUnmasked : InterruptsMasked;
#elif defined(__riscv) && __riscv_xlen == 32
        unsigned status;
        asm volatile("csrrci %0, mstatus, 8" : "=r"(status) : : "memory");
        return (status & (1U << 3)) ? InterruptsUnmasked : InterruptsMasked;
#else
#error GTOS CPU HAL requires a local interrupt backend
#endif
    }

    inline void RestoreInterrupts(InterruptState state) {
        if (state != InterruptsUnmasked) return;
#if defined(GTOS_MEMORY_TEST) || defined(GTOS_CPU_TEST)
        return;
#elif defined(__i386__)
        // Restore IF only, leaving compiler-owned arithmetic flags untouched.
        asm volatile("sti" : : : "memory");
#elif defined(__riscv) && __riscv_xlen == 32
        asm volatile("csrsi mstatus, 8" : : : "memory");
#endif
    }

} } }
#endif
