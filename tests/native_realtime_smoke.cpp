// The existing acceptance entry supplies only trusted boot and peer helpers.
#define NativeProcessSmoke NativeRealtimeUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include <memory/criticalsection.h>
#include "../apps/native_realtime_probe/record.h"
#ifndef GTOS_REALTIME_UNSUPPORTED
#define GTOS_REALTIME_UNSUPPORTED 0
#endif
namespace {
    volatile uint64_t realtimeIrqs;
    uint32_t realtimeCalls, realtimeErrors, realtimePreserved;
    PhysicalMemoryManager* realtimeFrames;
    uint64_t observedAnchor;
    bool haveAnchor;
    void RealtimeFinish(bool pass) __attribute__((noreturn));
    void RealtimeFinish(bool pass) {
        printf(pass ? (char*)"NATIVE REALTIME SMOKE PASS\n" : (char*)"NATIVE REALTIME SMOKE FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void RealtimeRequire(bool pass, const char* why) {
        if (!pass) { printf((char*)"FAILED REALTIME "); printf((char*)why); printf((char*)"\n"); RealtimeFinish(false); }
    }
    void RealtimeHex64(uint64_t value) { printfHex32((uint32_t)(value >> 32)); printfHex32((uint32_t)value); }
    void RealtimeRtcLog(const char* kind, const NativeRtcSnapshot& value) {
        printf((char*)"REALTIME RTC "); printf((char*)kind); printf((char*)" bytes=");
        const uint8_t* p = (const uint8_t*)&value;
        for (uint32_t i = 0; i < sizeof(value); ++i) { printf((char*)" "); printfHex32(p[i]); }
        printf((char*)"\n");
    }
}
namespace gtos {
    struct NativeClockFixture {
        static bool CopyCurrent(NativeRuntime& runtime, void* out, uint32_t address, uint32_t bytes) {
            NativeRuntime::Slot* slot = runtime.Current();
            return slot && slot->space.CopyFromUser(out, address, bytes);
        }
    };
}
namespace {
    class RealtimeTimerTap : public InterruptHandler {
    public:
        RealtimeTimerTap(InterruptsManager& interrupts) : InterruptHandler(&interrupts, 0x20) {}
        uint32_t HandlerInterrupt(uint32_t esp) { ++realtimeIrqs; return esp; }
    };
    class RealtimeSyscallTap : public InterruptHandler {
        NativeRuntime& runtime;
        TaskManager& tasks;
        SyscallHandler& original;
    public:
        RealtimeSyscallTap(InterruptsManager& interrupts, NativeRuntime& current, TaskManager& scheduler, SyscallHandler& handler)
            : InterruptHandler(&interrupts, 0x80), runtime(current), tasks(scheduler), original(handler) {}
        uint32_t HandlerInterrupt(uint32_t esp) {
            CPUState* cpu = (CPUState*)esp;
            if (cpu->eax != GTOS_SYS_REALTIME_READ) return original.HandlerInterrupt(esp);
            const CPUState registers = *cpu;
            const uint64_t irqs = realtimeIrqs;
            GtosClockReadResult before, after;
            RealtimeRequire(tasks.ReadClock(before) == 0, "real monotonic snapshot before syscall");
            const uint32_t free = realtimeFrames->getStatistics().freeFrames;
            GtosRealtimeReadRequest request = {};
            const bool copied = cpu->ecx == 16 && NativeClockFixture::CopyCurrent(runtime, &request, cpu->ebx, 16);
            const uint32_t returned = original.HandlerInterrupt(esp);
            RealtimeRequire(returned == esp && realtimeIrqs == irqs && tasks.ReadClock(after) == 0
                && before.microseconds == after.microseconds && before.delivered_ticks == after.delivered_ticks
                && realtimeFrames->getStatistics().freeFrames == free, "queries cannot invent time or allocate frames");
            RealtimeRequire(cpu->ebx == registers.ebx && cpu->ecx == registers.ecx && cpu->edx == registers.edx
                && cpu->esi == registers.esi && cpu->edi == registers.edi && cpu->ebp == registers.ebp
                && cpu->eip == registers.eip && cpu->esp == registers.esp && cpu->cs == registers.cs
                && cpu->ss == registers.ss && cpu->eflags == registers.eflags, "realtime preserves return frame and registers");
            ++realtimeCalls; ++realtimePreserved;
            if ((int32_t)cpu->eax < 0) ++realtimeErrors;
            else {
                GtosRealtimeReadResult result;
                RealtimeRequire(copied && NativeClockFixture::CopyCurrent(runtime, &result, request.result_va, 48),
                    "actual checked successful output");
                RealtimeRequire(result.version == 1 && result.unit == 1 && result.source == 1
                    && result.capabilities == 15 && result.resolution_us == 10000 && result.anchor_uncertainty_us == 1000000
                    && result.monotonic_microseconds == before.microseconds && result.delivered_ticks == irqs
                    && result.delivered_ticks == before.delivered_ticks && result.microseconds >= result.monotonic_microseconds,
                    "real coherent UTC and independently counted PIT IRQ");
                const uint64_t anchor = result.microseconds - result.monotonic_microseconds;
                if (!haveAnchor) { observedAnchor = anchor; haveAnchor = true; }
                RealtimeRequire(anchor == observedAnchor, "one immutable hardware epoch anchor per runtime");
                printf((char*)"REALTIME SAMPLE epoch="); RealtimeHex64(result.microseconds);
                printf((char*)" mono="); RealtimeHex64(result.monotonic_microseconds);
                printf((char*)" ticks="); RealtimeHex64(result.delivered_ticks); printf((char*)"\n");
            }
            return returned;
        }
    };
    RealtimeProbeRecord RealtimeRecord(NativeRuntime& runtime, uint32_t id) {
        RealtimeProbeRecord value = {};
        RealtimeRequire(runtime.ReadMemory(id, GTOS_REALTIME_RECORD_VA, &value, sizeof(value)), "read actual CPL3 record");
        return value;
    }
    uint32_t RealtimeCost(const Elf32LoadPlan& plan) {
        uint32_t seen[32] = {}, tables = 0;
        for (uint32_t i = 0; i < plan.segmentCount; ++i) {
            const Elf32LoadSegment& segment = plan.segments[i];
            if (!segment.memorySize) continue;
            for (uint32_t di = segment.virtualAddress >> 22; di <= (segment.virtualAddress + segment.memorySize - 1) >> 22; ++di) {
                const uint32_t bit = 1U << (di & 31);
                if (!(seen[di >> 5] & bit)) { seen[di >> 5] |= bit; ++tables; }
            }
        }
        const uint32_t stackDi = NativeRuntime::UserStackBottom >> 22;
        if (!(seen[stackDi >> 5] & (1U << (stackDi & 31)))) ++tables;
        return plan.pageCount + 2 + tables + 1;
    }
}
extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    printf((char*)"NATIVE REALTIME SMOKE BOOT\n");
    GlobalDescriptorTable gdt;
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    RealtimeRequire(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frames initialize");
    realtimeFrames = &frames;
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end, (const MultibootInfo*)multiboot, 0, 0};
    RealtimeRequire(paging.prepareIdentity(frames, config), "identity prepare");
    NativeRuntime runtime;
    RealtimeRequire(runtime.PrepareStacks(paging, frames) && paging.enable() && paging.sealForSharedProcessors(), "sealed native stacks");
    NativeRtcSnapshot before, after;
    RealtimeRequire(ReadNativeRtc(before), "actual valid PC RTC before activation"); RealtimeRtcLog("before", before);
    Port8Bit rtcIndex(0x70), rtcData(0x71);
    if (GTOS_REALTIME_UNSUPPORTED) {
        // Test-only hardware fault injection, restored immediately after activation.
        rtcIndex.Port8Bit::Write(0x0B); rtcData.Port8Bit::Write(before.format | 1U);
    }
    RealtimeRequire(runtime.Activate(tasks, gdt, paging, frames), "RTC failure does not break native activation");
    if (GTOS_REALTIME_UNSUPPORTED) { rtcIndex.Port8Bit::Write(0x0B); rtcData.Port8Bit::Write(before.format); }
    RealtimeRequire(ReadNativeRtc(after), "actual valid PC RTC restored"); RealtimeRtcLog("after", after);
    const uint32_t initial = frames.getStatistics().freeFrames, kernelDirectory = paging.getStatistics().directoryAddress;
    RealtimeTimerTap timer(interrupts); RealtimeSyscallTap syscallTap(interrupts, runtime, tasks, syscalls);
    const MultibootInfo* boot = (const MultibootInfo*)multiboot;
    RealtimeRequire((boot->flags & 8) && boot->moduleCount == 4, "exact four raw realtime modes");
    const MultibootModule* modules = (const MultibootModule*)boot->modules;
    Task ring0(&gdt, Ring0Task); RealtimeRequire(tasks.AddTask(&ring0), "ring0 peer admitted");
    const uint32_t peer = Create(runtime, 0x11223344, 0);
    asm volatile("outb %0,$0x43" : : "a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR & 255)));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR >> 8)));
    interrupts.Activate(); WaitTicks(tasks, 8);
    const uint32_t survivor = frames.getStatistics().freeFrames;
    RealtimeRequire(initial - survivor == 7, "raw CPL3 peer exact static cost");
    uint32_t cases = 0;
    for (uint32_t mode = 0; mode < 4; ++mode) {
        if (GTOS_REALTIME_UNSUPPORTED ? mode != 3 : mode == 3) continue;
        uint32_t peerBefore[4], peerAfter[4];
        RealtimeRequire(runtime.ReadMemory(peer, NativeRuntime::DataAddress, peerBefore, sizeof(peerBefore)), "peer before");
        const uint32_t ringBefore = ring0Progress, bootBefore = tasks.BootTicks();
        const uint32_t bytes = modules[mode].end - modules[mode].start;
        Elf32LoadPlan plan;
        RealtimeRequire(modules[mode].end > modules[mode].start && bytes <= 65536
            && ValidateElf32((const uint8_t*)modules[mode].start, bytes, plan), "bounded actual ELF32");
        const uint32_t cost = RealtimeCost(plan);
        uint32_t id;
        RealtimeRequire(runtime.CreateElf((const uint8_t*)modules[mode].start, bytes, id), "actual realtime CPL3 admission");
        if (mode == 2) {
            const uint32_t start = tasks.Ticks();
            while (RealtimeRecord(runtime, id).stage < 2) {
                RealtimeRequire(tasks.Ticks() - start < 500, "bounded realtime stage"); WaitTicks(tasks, 1);
            }
            RealtimeRequire(runtime.RequestExit(id, 73), "cancel yielding realtime user");
        }
        const NativeStatus status = WaitStopped(runtime, tasks, id);
        const RealtimeProbeRecord record = RealtimeRecord(runtime, id);
        RealtimeRequire(record.version == 1 && record.mode == mode && record.stage == 2 && !record.error
            && record.checks && record.abi && status.observedCs == 0x23 && status.observedCr3 == status.directory
            && status.directory != kernelDirectory && status.systemCalls, "actual CPL3 and record identity");
        RealtimeRequire(survivor - frames.getStatistics().freeFrames == cost + (mode == 3 ? 0 : 3), "queries allocate no frames");
        if (mode == 1) RealtimeRequire(status.faultVector == 14 && status.faultAddress == 0xBFFFC000U && status.faultError == 6
            && status.exitCode == (0x80000000U | 14), "exact writable guard user PF6");
        else RealtimeRequire(!status.faultVector && status.exitCode == (mode == 2 ? 73U : 0U), "normal or cancellation exit");
        if (mode != 3) {
            RealtimeRequire(record.rejected == 18 && record.sentinels == 1072 && record.base == 0x80000000U && record.handle
                && record.first.monotonic_microseconds >= record.monoFirst.microseconds
                && record.last.monotonic_microseconds <= record.monoLast.microseconds
                && record.last.microseconds > record.first.microseconds, "negative sentinels and live progress");
            uint8_t contents[8192];
            RealtimeRequire(runtime.ReadMemory(id, record.base, contents, sizeof(contents)), "retained mapped pages");
            for (uint32_t i = 0; i < sizeof(contents); ++i) RealtimeRequire(contents[i] == 0x37, "query never corrupts retained pages");
        } else RealtimeRequire(record.rejected == 1 && record.sentinels == 64 && !record.base && !record.first.microseconds,
            "invalid real RTC returns unsupported with intact output and working monotonic clock");
        InterruptGuard logGuard;
        printf((char*)"REALTIME CASE mode="); printfHex32(mode); printf((char*)" checks="); printfHex32(record.checks);
        printf((char*)" rejected="); printfHex32(record.rejected); printf((char*)" sentinels="); printfHex32(record.sentinels);
        printf((char*)" cs="); printfHex32(status.observedCs); printf((char*)" vector="); printfHex32(status.faultVector);
        printf((char*)" address="); printfHex32(status.faultAddress); printf((char*)" pf="); printfHex32(status.faultError);
        printf((char*)" exit="); printfHex32(status.exitCode); printf((char*)" cost="); printfHex32(cost);
        printf((char*)" retained="); printfHex32(survivor - frames.getStatistics().freeFrames); printf((char*)"\n");
        RealtimeRequire(runtime.Reap() == 1 && frames.getStatistics().freeFrames == survivor, "exact victim Reap");
        printf((char*)"REALTIME REAP free="); printfHex32(frames.getStatistics().freeFrames);
        printf((char*)" expected="); printfHex32(survivor); printf((char*)"\n"); ++cases;
        // Leave IF enabled for actual boot and both independent peers.
        asm volatile("sti" : : : "memory"); WaitTicks(tasks, 4);
        RealtimeRequire(runtime.ReadMemory(peer, NativeRuntime::DataAddress, peerAfter, sizeof(peerAfter))
            && peerAfter[0] == 0x11223344 && peerAfter[3] > peerBefore[3]
            && ring0Progress > ringBefore && tasks.BootTicks() > bootBefore, "boot and ring0/CPL3 peers survive");
    }
    RealtimeRequire(runtime.RequestExit(peer, 0) && runtime.Reap() == 1 && frames.getStatistics().freeFrames == initial,
        "final exact peer Reap and original allocator baseline");
    WaitTicks(tasks, 4);
    InterruptGuard logGuard;
    printf((char*)"REALTIME FINAL free="); printfHex32(frames.getStatistics().freeFrames); printf((char*)" expected="); printfHex32(initial);
    printf((char*)" cases="); printfHex32(cases); printf((char*)" calls="); printfHex32(realtimeCalls);
    printf((char*)" errors="); printfHex32(realtimeErrors); printf((char*)" preserved="); printfHex32(realtimePreserved);
    printf((char*)" anchor="); RealtimeHex64(observedAnchor); printf((char*)" irqs="); RealtimeHex64(realtimeIrqs); printf((char*)"\n");
    RealtimeFinish(true);
}
