// Kernel's unrelated size_t typedef is 32-bit; host sanitizers run on x86-64.
// Keep the real uint32_t policy/algorithm while avoiding that unused typedef.
#define size_t gtos_test_kernel_size_t
#include <drivers/ata_wait.h>
#undef size_t
#include <cassert>
#include <cstdio>
using namespace gtos::drivers;
static uint32_t ticks;
static uint32_t Clock() { return ticks; }
struct Status {
    uint32_t reads, readyAfter, tickEvery;
    uint8_t busy, ready;
    uint8_t Read() {
        ++reads;
        if (tickEvery && !(reads%tickEvery)) ++ticks;
        return reads>readyAfter ? ready : busy;
    }
};
static AtaWaitResult run(Status& port,bool drq,bool interrupts,uint32_t duration=500) {
    AtaWaitBudget budget(Clock,duration,interrupts);return PollAta(port,drq,budget);
}
int main() {
    ticks=0;Status late={0,1000010,100000,0xC0,0x40};
    assert(run(late,false,true)==AtaReady && late.reads==1000011 && ticks==10);
    ticks=0;Status boot={0,1000010,100000,0xC0,0x40};
    assert(run(boot,false,false)==AtaTimedOut && boot.reads==AtaWaitBudget::BootPollLimit);
    ticks=0;Status deadline={0,0xFFFFFFFFU,1,0xC0,0x40};
    assert(run(deadline,false,true,500)==AtaTimedOut && deadline.reads>=500 && deadline.reads<=756);
    ticks=0xFFFFFFF8U;Status wrap={0,0xFFFFFFFFU,1,0xC0,0x40};
    assert(run(wrap,false,true,20)==AtaTimedOut && wrap.reads<=276);
    ticks=0;Status frozen={0,0xFFFFFFFFU,0,0xC0,0x40};
    assert(run(frozen,false,true)==AtaTimedOut && frozen.reads==AtaWaitBudget::ClockPollLimit);
    const uint8_t faults[]={0,0xFF,0x41,0x60};
    for(unsigned i=0;i<4;i++) {
        ticks=0;Status fault={0,0,0,0,faults[i]};
        assert(run(fault,true,true)==(i<2?AtaMissing:AtaDeviceError) && fault.reads==1);
    }
    ticks=0;Status drq={0,300,1,0x40,0x48};
    assert(run(drq,true,true)==AtaReady && drq.reads==301);
    ticks=0;Status noDrq={0,0xFFFFFFFFU,1,0x40,0x48};
    assert(run(noDrq,true,true,20)==AtaTimedOut);
    ticks=0;Status ready={0,0,0,0,0x40};
    assert(run(ready,false,true)==AtaReady && ready.reads==1);
    ticks=0;Status noClock={0,0xFFFFFFFFU,0,0xC0,0x40};
    AtaWaitBudget bounded(0,500,true);
    assert(PollAta(noClock,false,bounded)==AtaTimedOut && noClock.reads==AtaWaitBudget::BootPollLimit);
    std::puts("ATA wait actual policy PASS: delayed BSY/flush, boot, deadline, wrap, stalled clock, ERR/DF/no-device, DRQ");
}
