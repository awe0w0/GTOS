#ifndef GTOS_DRIVERS_ATA_WAIT_H
#define GTOS_DRIVERS_ATA_WAIT_H
#include <common/types.h>
namespace gtos { namespace drivers {
// Runtime callers use their monotonic clock. IF-clear early boot has no timer.
// A separate finite poll ceiling also bounds a stalled/misconfigured clock.
class AtaWaitBudget {
public:
    typedef uint32_t (*Clock)();
    enum { BootPollLimit=1000000U, ClockPollLimit=100000000U };
private:
    Clock clock;
    uint32_t start, timeout, polls, limit;
public:
    AtaWaitBudget(Clock source, uint32_t duration, bool interruptsEnabled)
        : clock(interruptsEnabled ? source : 0), start(0), timeout(duration),
          polls(0), limit(clock ? ClockPollLimit : BootPollLimit) {
        if (clock) start=clock();
    }
    bool TakePoll() {
        if (polls==limit) return false;
        // Avoid a callback on each PIO read; elapsed time is wrap-safe.
        if (clock && !(polls&255U) && uint32_t(clock()-start)>=timeout) return false;
        ++polls; return true;
    }
};
enum AtaWaitResult { AtaReady, AtaMissing, AtaDeviceError, AtaTimedOut };
template<class StatusPort> AtaWaitResult PollAta(StatusPort& port, bool drq, AtaWaitBudget& budget) {
    while (budget.TakePoll()) {
        const uint8_t status=port.Read();
        if (status==0 || status==0xFF) return AtaMissing;
        if (status&0x80) { asm volatile("pause"); continue; }
        if (status&0x21) return AtaDeviceError;
        if (!drq || (status&8)) return AtaReady;
        asm volatile("pause");
    }
    return AtaTimedOut;
}
} }
#endif
