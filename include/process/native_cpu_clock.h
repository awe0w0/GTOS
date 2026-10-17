#ifndef __GTOS__PROCESS__NATIVE_CPU_CLOCK_H
#define __GTOS__PROCESS__NATIVE_CPU_CLOCK_H
#include <common/types.h>
#include <process/clock_abi.h>
#include <process/cpu_clock_abi.h>
namespace gtos {
    class Task;
    class TaskManager;
    struct NativeCpuClockFixture;
    namespace process {
        class NativeCpuClockCounter {
            friend class ::gtos::Task;
            friend class ::gtos::TaskManager;
            friend struct ::gtos::NativeCpuClockFixture;
            uint64_t microseconds, chargedTicks;
            uint32_t fraction;
            bool exhausted;
            void Reset() {
                microseconds = chargedTicks = 0;
                fraction = 0; exhausted = false;
            }
            void Advance() {
                if (exhausted) return;
                uint32_t nextFraction = fraction + 373182U;
                uint32_t delta = 9999U;
                if (nextFraction >= GTOS_CLOCK_PIT_INPUT_HZ) {
                    nextFraction -= GTOS_CLOCK_PIT_INPUT_HZ; ++delta;
                }
                const uint64_t maximum = ~(uint64_t)0;
                if (microseconds > maximum - delta || chargedTicks == maximum) {
                    exhausted = true; return;
                }
                microseconds += delta; ++chargedTicks; fraction = nextFraction;
            }
            int Read(uint32_t scope, GtosCpuClockReadResult& result) const {
                if (exhausted) return GTOS_CPU_CLOCK_ERR_OVERFLOW;
                result.version = GTOS_CPU_CLOCK_ABI_VERSION;
                result.scope = scope;
                result.unit = GTOS_CPU_CLOCK_UNIT_MICROSECONDS;
                result.source = GTOS_CPU_CLOCK_SOURCE_PIT_RUNNING_SAMPLE;
                result.capabilities = GTOS_CPU_CLOCK_REQUIRED_CAPABILITIES;
                result.resolution_us = GTOS_CLOCK_RESOLUTION_US;
                result.microseconds = microseconds; result.charged_ticks = chargedTicks;
                result.pit_input_hz = GTOS_CLOCK_PIT_INPUT_HZ;
                result.pit_divisor = GTOS_CLOCK_PIT_DIVISOR;
                return 0;
            }
            NativeCpuClockCounter(const NativeCpuClockCounter&);
            NativeCpuClockCounter& operator=(const NativeCpuClockCounter&);
        public:
            NativeCpuClockCounter() { Reset(); }
            static_assert((uint64_t)GTOS_CLOCK_PIT_DIVISOR * 1000000U
                == (uint64_t)9999U * GTOS_CLOCK_PIT_INPUT_HZ + 373182U,
                "CPU accounting must match the actual PIT divisor");
        };
    }
}
#endif
