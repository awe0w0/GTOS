#define NativeProcessSmoke SleepUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include <memory/criticalsection.h>
#include "record.h"
namespace {
    uint8_t retained[8192];
    void SleepFinish(bool pass) __attribute__((noreturn));
    void SleepFinish(bool pass) {
        printf(pass ? (char*)"NATIVE C SLEEP SMOKE PASS\n" : (char*)"NATIVE C SLEEP SMOKE FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void SleepRequire(bool pass, const char* why) {
        if (!pass) {
            asm volatile("cli" : : : "memory");
            printf((char*)"FAILED SLEEP "); printf((char*)why); printf((char*)"\n"); SleepFinish(false);
        }
    }
    SleepRecord Read(NativeRuntime& runtime, uint32_t id) {
        SleepRecord result = {};
        SleepRequire(runtime.ReadMemory(id, GTOS_SLEEP_RECORD_VA, &result, sizeof(result)), "retained record");
        return result;
    }
    uint32_t Cost(const Elf32LoadPlan& plan) {
        uint32_t seen[32] = {}, tables = 0;
        for (uint32_t i = 0; i < plan.segmentCount; ++i) {
            const Elf32LoadSegment& s = plan.segments[i];
            if (!s.memorySize) continue;
            for (uint32_t di = s.virtualAddress >> 22; di <= (s.virtualAddress + s.memorySize - 1) >> 22; ++di) {
                const uint32_t bit = 1U << (di & 31);
                if (!(seen[di >> 5] & bit)) { seen[di >> 5] |= bit; ++tables; }
            }
        }
        if (!(seen[(NativeRuntime::UserStackBottom >> 22) >> 5] & (1U << ((NativeRuntime::UserStackBottom >> 22) & 31)))) ++tables;
        return plan.pageCount + 2 + tables + 1;
    }
}
namespace gtos {
    struct NativeClockFixture {
        static bool CopyCurrent(NativeRuntime& runtime, void* out, uint32_t va, uint32_t bytes) {
            NativeRuntime::Slot* slot = runtime.Current();
            return slot && slot->space.CopyFromUser(out, va, bytes);
        }
    };
}
namespace {
    struct SleepEvent { uint64_t before, after, first, last; uint32_t before_calls, after_calls, clocks, yields; } sleepEvents[12];
    uint32_t currentSleepId, waitClocks, waitYields, faultClockCount[3];
    volatile uint64_t sleepIrqs;
    class SleepTimerTap : public InterruptHandler {
    public:
        SleepTimerTap(InterruptsManager& interrupts) : InterruptHandler(&interrupts, 0x20) {}
        uint32_t HandlerInterrupt(uint32_t esp) { ++sleepIrqs; return esp; }
    };
    class SleepSyscallTap : public InterruptHandler {
        NativeRuntime& runtime; TaskManager& tasks; SyscallHandler& original;
    public:
        SleepSyscallTap(InterruptsManager& interrupts, NativeRuntime& r, TaskManager& t, SyscallHandler& o)
            : InterruptHandler(&interrupts, 0x80), runtime(r), tasks(t), original(o) {}
        uint32_t HandlerInterrupt(uint32_t esp) {
            CPUState* cpu = (CPUState*)esp;
            if (cpu->eax != GTOS_SYS_CLOCK_READ && cpu->eax != GTOS_SYS_YIELD) return original.HandlerInterrupt(esp);
            NativeStatus status = {};
            uint32_t cr3; asm volatile("mov %%cr3,%0" : "=r"(cr3));
            SleepRequire(runtime.Status(currentSleepId, status) && status.live && status.directory == cr3
                && cpu->cs == 0x23, "actual native sleep caller");
            SleepRecord record;
            SleepRequire(NativeClockFixture::CopyCurrent(runtime, &record, GTOS_SLEEP_RECORD_VA, sizeof(record)), "current private record");
            if (cpu->eax == GTOS_SYS_YIELD) {
                if (record.stage == 3) ++waitYields;
                if (record.active) {
                    SleepRequire(record.active <= 12 && record.marker == 2, "real sleep yield phase");
                    ++sleepEvents[record.active - 1].yields;
                }
                return original.HandlerInterrupt(esp);
            }
            const CPUState registers = *cpu;
            GtosClockReadRequest request;
            SleepRequire(NativeClockFixture::CopyCurrent(runtime, &request, cpu->ebx, sizeof(request))
                && cpu->ecx == sizeof(request) && request.version == 1 && request.clock_id == 1
                && !request.flags && request.result_bytes == 48, "real C monotonic request");
            GtosClockReadResult before, result, after;
            SleepRequire(tasks.ReadClock(before) == 0 && sleepIrqs == before.delivered_ticks, "independently counted PIT IRQs");
            const uint32_t returned = original.HandlerInterrupt(esp);
            SleepRequire(returned == esp && cpu->eax == 0 && tasks.ReadClock(after) == 0
                && before.microseconds == after.microseconds && before.delivered_ticks == after.delivered_ticks
                && NativeClockFixture::CopyCurrent(runtime, &result, request.result_va, sizeof(result))
                && result.microseconds == before.microseconds && result.delivered_ticks == before.delivered_ticks,
                "coherent genuine production monotonic result");
            SleepRequire(cpu->ebx == registers.ebx && cpu->ecx == registers.ecx && cpu->edx == registers.edx
                && cpu->esi == registers.esi && cpu->edi == registers.edi && cpu->ebp == registers.ebp
                && cpu->eip == registers.eip && cpu->esp == registers.esp && cpu->cs == registers.cs
                && cpu->ss == registers.ss && cpu->eflags == registers.eflags, "clock non-result frame");
            if (record.marker >= 10 && record.marker <= 12) {
                const uint32_t index = record.marker - 10; ++faultClockCount[index];
                if (index == 0) cpu->eax = (uint32_t)-38;
                else if (index == 1 && faultClockCount[index] == 2) cpu->eax = (uint32_t)-75;
                else if (index == 2) cpu->eax = 1;
            } else if (record.active) {
                SleepRequire(record.active <= 12, "bounded event index");
                SleepEvent& event = sleepEvents[record.active - 1];
                if (record.marker == 1) { event.before = result.delivered_ticks; ++event.before_calls; }
                else if (record.marker == 3) { event.after = result.delivered_ticks; ++event.after_calls; }
                else {
                    SleepRequire(record.marker == 2, "wait clock phase");
                    if (!event.clocks) event.first = result.delivered_ticks;
                    event.last = result.delivered_ticks; ++event.clocks;
                }
            } else if (record.stage == 3) ++waitClocks;
            return returned;
        }
    };
    void SleepHex64(uint64_t value) { printfHex32(value >> 32); printfHex32(value); }
}

asm(".section .text.native_sleep_peer,\"ax\"\n.balign 16\n"
    ".global native_user_start,native_user_end\nnative_user_start:\n"
    "incl 0x4000200c\njmp native_user_start\nnative_user_end:\n.text\n");
extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    printf((char*)"NATIVE C SLEEP SMOKE BOOT\n");
    GlobalDescriptorTable gdt; TaskManager tasks; InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80); PhysicalMemoryManager frames;
    SleepRequire(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "initialize frames");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end, (uint32_t)&kernel_readonly_start,
        (uint32_t)&kernel_readonly_end, (const MultibootInfo*)multiboot, 0, 0};
    SleepRequire(paging.prepareIdentity(frames, config), "identity paging");
    NativeRuntime runtime;
    SleepRequire(runtime.PrepareStacks(paging, frames) && paging.enable() && paging.sealForSharedProcessors(), "guarded sealed paging");
    SleepRequire(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2)
        && runtime.FpEnabled() && runtime.FpError() == NativeFpOk, "activate production FP");
    SleepSyscallTap syscallTap(interrupts, runtime, tasks, syscalls); SleepTimerTap timerTap(interrupts);
    const uint32_t kernelCr3 = paging.getStatistics().directoryAddress;
    const uint32_t initial = frames.getStatistics().freeFrames;
    const MultibootInfo* boot = (const MultibootInfo*)multiboot;
    SleepRequire((boot->flags & 8) && boot->moduleCount == 4, "four actual C sleep ELF modes");
    const MultibootModule* modules = (const MultibootModule*)boot->modules;
    Task ring0(&gdt, Ring0Task); SleepRequire(tasks.AddTask(&ring0), "ring0 peer");
    const uint32_t peer = Create(runtime, 0x11223344, 0);
    asm volatile("outb %0,$0x43" : : "a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR & 255)));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR >> 8)));
    interrupts.Activate(); WaitTicks(tasks, 8);
    const uint32_t survivor = frames.getStatistics().freeFrames;
    SleepRequire(initial - survivor == 7, "raw peer exact cost");
    for (uint32_t iteration = 0; iteration < 5; ++iteration) {
        const uint32_t mode = iteration == 4 ? 0 : iteration;
        uint32_t peerBefore[4]; SleepRequire(runtime.ReadMemory(peer, NativeRuntime::DataAddress, peerBefore, sizeof(peerBefore)), "peer snapshot");
        const uint32_t ringBefore = ring0Progress, bootBefore = tasks.BootTicks();
        const uint32_t bytes = modules[mode].end - modules[mode].start;
        Elf32LoadPlan plan; SleepRequire(modules[mode].end > modules[mode].start
            && ValidateElf32((const uint8_t*)modules[mode].start, bytes, plan), "bounded production ELF admission");
        const uint32_t cost = Cost(plan); uint32_t id;
        const NativeFpStatistics beforeFp = runtime.FpStatistics();
        { InterruptGuard guard;
          waitClocks = waitYields = 0;
          for (uint32_t i = 0; i < 3; ++i) faultClockCount[i] = 0;
          for (uint32_t i = 0; i < 12; ++i) sleepEvents[i] = SleepEvent();
          SleepRequire(runtime.CreateElf((const uint8_t*)modules[mode].start, bytes, id), "CreateElf");
          currentSleepId = id; }
        if (mode >= 2) {
            const uint32_t start = tasks.Ticks();
            for (;;) {
                SleepRecord record; NativeStatus status;
                { InterruptGuard guard; record = Read(runtime, id); SleepRequire(runtime.Status(id, status), "waiting status"); }
                if (record.stage == 3 && waitClocks >= 2 && waitYields >= 1) break;
                SleepRequire(status.live && tasks.Ticks() - start < 1000, "bounded cancellation phase"); WaitTicks(tasks, 1);
            }
            SleepRequire(runtime.RequestExit(id, 73), "external cancel");
        }
        const NativeStatus status = WaitStopped(runtime, tasks, id);
        const SleepRecord record = Read(runtime, id);
        { InterruptGuard guard;
          printf((char*)"SLEEP CASE mode="); printfHex32(mode); printf((char*)" stage="); printfHex32(record.stage);
          printf((char*)" error="); printfHex32(record.error); printf((char*)" checks="); printfHex32(record.checks);
          printf((char*)" samples="); printfHex32(record.count); printf((char*)" exit="); printfHex32(status.exitCode);
          printf((char*)" vector="); printfHex32(status.faultVector); printf((char*)" pf="); printfHex32(status.faultError);
          printf((char*)" cr2="); printfHex32(status.faultAddress); printf((char*)" cs="); printfHex32(status.observedCs);
          printf((char*)" cr3="); printfHex32(status.observedCr3); printf((char*)" kernel_cr3="); printfHex32(kernelCr3);
          printf((char*)" rejected="); printfHex32(record.rejected); printf((char*)" errno="); printfHex32(record.errno_value);
          printf((char*)" wait_clocks="); printfHex32(waitClocks); printf((char*)" wait_yields="); printfHex32(waitYields);
          printf((char*)" load_pages="); printfHex32(plan.pageCount); printf((char*)" cost="); printfHex32(cost); printf((char*)"\n"); }
        SleepRequire(record.version == 1 && record.mode == mode && !record.error && record.checks
            && record.base == 0x80000000U && record.handle && record.count == 12,
            "actual C sleep checks");
        SleepRequire(status.observedCs == 0x23 && status.observedCr3 == status.directory && status.directory != kernelCr3
            && status.directory >= 4096, "real CPL3/private CR3");
        SleepRequire(record.heap_base == 0x80002000U && record.heap_bytes == 65536
            && record.errno_va >= record.heap_base && record.errno_va <= record.heap_base + record.heap_bytes - 4
            && record.errno_value == 0x5533, "real private TLS heap");
        uint32_t savedErrno; SleepRequire(runtime.ReadMemory(id, record.errno_va, &savedErrno, 4) && savedErrno == record.errno_value, "retained errno");
        SleepRequire(record.stage == (mode == 0 ? 2U : 3U), "completed diagnostic stage");
        if (mode == 1) SleepRequire(status.faultVector == 14 && status.faultError == 4
            && status.faultAddress == 0xBFFFCFFCU && status.exitCode == 0x8000000EU, "explicit invalid request contained");
        else SleepRequire(!status.faultVector && status.exitCode == (mode >= 2 ? 73U : 0U), "normal exit or cancellation");
        SleepRequire(record.count == 12 && record.rejected == 8 && faultClockCount[0] == 1
            && faultClockCount[1] == 2 && faultClockCount[2] == 1, "native validation/error propagation without signal fabrication");
        if (mode >= 2) {
            int64_t request[2];
            SleepRequire(record.wait_va && runtime.ReadMemory(id, record.wait_va, request, sizeof(request))
                && request[0] == 0x7fffffffffffffffLL && request[1] == (mode == 2 ? 999999999 : 0), "full-domain request retained during cancellation");
        }
        for (uint32_t i = 0; i < 12; ++i) {
            const SleepEvent& event = sleepEvents[i]; const SleepSample& sample = record.samples[i];
            SleepRequire(event.before_calls == 1 && event.after_calls == 1 && !sample.status
                && sample.requested_seconds == 0 && sample.before_seconds >= 0
                && sample.after_seconds >= 0, "one genuine sample pair");
            if (!sample.requested_nanoseconds) SleepRequire(!event.clocks && !event.yields, "zero wait no scheduling");
            else SleepRequire(event.clocks >= 2 && event.yields && event.last > event.first, "actual elapsed sleep path");
            InterruptGuard guard;
            printf((char*)"SLEEP EVENT index="); printfHex32(i); printf((char*)" kind="); printfHex32(sample.kind);
            printf((char*)" request="); SleepHex64(sample.requested_nanoseconds);
            printf((char*)" before_ticks="); SleepHex64(event.before); printf((char*)" after_ticks="); SleepHex64(event.after);
            printf((char*)" first_ticks="); SleepHex64(event.first); printf((char*)" last_ticks="); SleepHex64(event.last);
            printf((char*)" before_seconds="); SleepHex64(sample.before_seconds); printf((char*)" before_ns="); SleepHex64(sample.before_nanoseconds);
            printf((char*)" after_seconds="); SleepHex64(sample.after_seconds); printf((char*)" after_ns="); SleepHex64(sample.after_nanoseconds);
            printf((char*)" before_calls="); printfHex32(event.before_calls); printf((char*)" after_calls="); printfHex32(event.after_calls);
            printf((char*)" clocks="); printfHex32(event.clocks); printf((char*)" yields="); printfHex32(event.yields); printf((char*)"\n");
        }
        SleepRequire(runtime.ReadMemory(id, record.base, retained, sizeof(retained)), "retained all sentinel pages");
        for (uint32_t i = 0; i < sizeof(retained); ++i) {
            if (i >= 4088 && i < 4104) continue;
            SleepRequire(retained[i] == (uint8_t)(i ^ 0x5D), "surrounding owned bytes");
        }
        SleepRequire(survivor - frames.getStatistics().freeFrames == cost + 19 && !status.live && !status.reaped, "exact stopped cost");
        SleepRequire(runtime.Reap() == 1 && frames.getStatistics().freeFrames == survivor, "exact victim Reap");
        const NativeFpStatistics afterFp = runtime.FpStatistics();
        SleepRequire(afterFp.initialized == beforeFp.initialized + 1 && afterFp.invalidated == beforeFp.invalidated + 1
            && afterFp.saves > beforeFp.saves && afterFp.restores > beforeFp.restores && !afterFp.invariantFailures, "real FP lifecycle");
        WaitTicks(tasks, 8);
        uint32_t peerAfter[4]; NativeStatus peerStatus;
        SleepRequire(runtime.ReadMemory(peer, NativeRuntime::DataAddress, peerAfter, sizeof(peerAfter))
            && runtime.Status(peer, peerStatus) && peerStatus.live && !peerStatus.systemCalls && peerAfter[3] != peerBefore[3]
            && peerAfter[0] == 0x11223344 && ring0Progress != ringBefore && tasks.BootTicks() > bootBefore, "peers continue");
        { InterruptGuard guard;
          printf((char*)"SLEEP REAP free="); printfHex32(survivor); printf((char*)" expected="); printfHex32(frames.getStatistics().freeFrames);
          printf((char*)" fp_initialized="); printfHex32(afterFp.initialized); printf((char*)" fp_invalidated="); printfHex32(afterFp.invalidated);
          printf((char*)" fp_saves="); printfHex32(afterFp.saves); printf((char*)" fp_restores="); printfHex32(afterFp.restores);
          printf((char*)" fp_failures="); printfHex32(afterFp.invariantFailures); printf((char*)"\n"); }
    }
    SleepRequire(runtime.RequestExit(peer, 0) && runtime.Reap() == 1 && frames.getStatistics().freeFrames == initial, "allocator baseline");
    const NativeFpStatistics finalFp = runtime.FpStatistics();
    SleepRequire(finalFp.initialized == 6 && finalFp.invalidated == 6 && !finalFp.invariantFailures, "all FP owners reclaimed");
    { InterruptGuard guard;
      printf((char*)"SLEEP FINAL free="); printfHex32(frames.getStatistics().freeFrames); printf((char*)" expected="); printfHex32(initial);
      printf((char*)" fp_initialized="); printfHex32(finalFp.initialized); printf((char*)" fp_invalidated="); printfHex32(finalFp.invalidated);
      printf((char*)" fp_failures="); printfHex32(finalFp.invariantFailures); printf((char*)"\n"); SleepFinish(true); }
}
