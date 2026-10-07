#ifndef __GTOS__PROCESS__NATIVE_CLOCK_H
#define __GTOS__PROCESS__NATIVE_CLOCK_H
#include <common/types.h>
#include <process/clock_abi.h>
namespace gtos {
    class TaskManager;
    struct NativeClockFixture;
    namespace process {
        // Scheduler-owned BSP state. Only valid timer Dispatch advances it;
        // TaskManager guards snapshots. No process lifetime can reset it.
        class NativeClockCounter {
            friend class ::gtos::TaskManager;
            // Acceptance fixture seeds a private instance only before PIT starts.
            friend struct ::gtos::NativeClockFixture;
            uint64_t microseconds, deliveredTicks;
            uint32_t fraction;
            bool exhausted;
            static const uint32_t WholeMicroseconds = 9999U;
            static const uint32_t FractionPerTick = 373182U;
            void Advance() {
                if (exhausted) return;
                uint32_t nextFraction = fraction + FractionPerTick;
                uint32_t delta = WholeMicroseconds;
                if (nextFraction >= GTOS_CLOCK_PIT_INPUT_HZ) {
                    nextFraction -= GTOS_CLOCK_PIT_INPUT_HZ;
                    ++delta;
                }
                const uint64_t maximum = ~(uint64_t)0;
                if (microseconds > maximum - delta || deliveredTicks == maximum) {
                    exhausted = true;
                    return;
                }
                const uint64_t nextMicroseconds = microseconds + delta;
                const uint64_t nextTicks = deliveredTicks + 1;
                microseconds = nextMicroseconds;
                deliveredTicks = nextTicks;
                fraction = nextFraction;
            }
            int Read(GtosClockReadResult& result) const {
                if (exhausted) return GTOS_CLOCK_ERR_OVERFLOW;
                result.version = GTOS_CLOCK_ABI_VERSION;
                result.clock_id = GTOS_CLOCK_ID_MONOTONIC;
                result.unit = GTOS_CLOCK_UNIT_MICROSECONDS;
                result.source = GTOS_CLOCK_SOURCE_PIT_DELIVERED_IRQ;
                result.capabilities = GTOS_CLOCK_REQUIRED_CAPABILITIES;
                result.resolution_us = GTOS_CLOCK_RESOLUTION_US;
                result.microseconds = microseconds;
                result.delivered_ticks = deliveredTicks;
                result.pit_input_hz = GTOS_CLOCK_PIT_INPUT_HZ;
                result.pit_divisor = GTOS_CLOCK_PIT_DIVISOR;
                return 0;
            }
            NativeClockCounter(const NativeClockCounter&);
            NativeClockCounter& operator=(const NativeClockCounter&);
        public:
            NativeClockCounter() : microseconds(0), deliveredTicks(0), fraction(0), exhausted(false) {}
            static_assert((uint64_t)GTOS_CLOCK_PIT_DIVISOR * 1000000U
                == (uint64_t)WholeMicroseconds * GTOS_CLOCK_PIT_INPUT_HZ + FractionPerTick,
                "PIT fraction must match the programmed divisor");
            static_assert(FractionPerTick < GTOS_CLOCK_PIT_INPUT_HZ
                && GTOS_CLOCK_RESOLUTION_US == WholeMicroseconds + 1, "Coarse PIT resolution");
        };
    }
}
#endif