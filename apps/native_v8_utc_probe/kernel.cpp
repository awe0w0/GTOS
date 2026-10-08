// Reuse only the qualified boot, allocator and raw-peer helpers.
#define NativeProcessSmoke UtcUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include <memory/criticalsection.h>
#include <hardwarecommunication/port.h>
#include "record.h"
#ifndef GTOS_UTC_UNSUPPORTED
#define GTOS_UTC_UNSUPPORTED 0
#endif
namespace {
    PhysicalMemoryManager* utcFrames;
    volatile uint64_t utcIrqs;
    uint32_t utcId, utcKernelCr3, utcCalls, monoCalls, errors;
    uint64_t utcAnchor;
    bool anchored;
    struct Event { uint32_t kind, present, error; uint64_t value, mono, ticks; } events[8];
    uint8_t retained[8192];
    void UtcFinish(bool pass) __attribute__((noreturn));
    void UtcFinish(bool pass) {
        printf(pass ? (char*)"NATIVE V8 UTC SMOKE PASS\n" : (char*)"NATIVE V8 UTC SMOKE FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void UtcRequire(bool pass, const char* why) {
        if (!pass) {
            asm volatile("cli" : : : "memory");
            printf((char*)"FAILED V8 UTC "); printf((char*)why); printf((char*)"\n"); UtcFinish(false);
        }
    }
    void UtcHex64(uint64_t value) { printfHex32(value >> 32); printfHex32(value); }
    uint32_t UtcCr3() { uint32_t result; asm volatile("mov %%cr3,%0" : "=r"(result)); return result; }
    void UtcRtc(const char* label, const NativeRtcSnapshot& rtc) {
        printf((char*)"UTC RTC "); printf((char*)label); printf((char*)" bytes=");
        const uint8_t* p = (const uint8_t*)&rtc;
        for (uint32_t i = 0; i < sizeof(rtc); ++i) { printf((char*)" "); printfHex32(p[i]); }
        printf((char*)"\n");
    }
    UtcRecord UtcRead(NativeRuntime& runtime, uint32_t id) {
        UtcRecord result = {};
        UtcRequire(runtime.ReadMemory(id, GTOS_UTC_RECORD_VA, &result, sizeof(result)), "actual retained user record");
        return result;
    }
    uint32_t UtcCost(const Elf32LoadPlan& plan) {
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
    class UtcTimerTap : public InterruptHandler {
    public:
        UtcTimerTap(InterruptsManager& interrupts) : InterruptHandler(&interrupts, 0x20) {}
        uint32_t HandlerInterrupt(uint32_t esp) { ++utcIrqs; return esp; }
    };
    class UtcSyscallTap : public InterruptHandler {
        NativeRuntime& runtime; TaskManager& tasks; SyscallHandler& original;
    public:
        UtcSyscallTap(InterruptsManager& interrupts, NativeRuntime& r, TaskManager& t, SyscallHandler& o)
            : InterruptHandler(&interrupts, 0x80), runtime(r), tasks(t), original(o) {}
        uint32_t HandlerInterrupt(uint32_t esp) {
            CPUState* cpu = (CPUState*)esp;
            if (cpu->eax != GTOS_SYS_REALTIME_READ && cpu->eax != GTOS_SYS_CLOCK_READ)
                return original.HandlerInterrupt(esp);
            const CPUState registers = *cpu;
            NativeStatus caller = {};
            UtcRequire(runtime.Status(utcId, caller) && caller.live && caller.directory == UtcCr3()
                && caller.directory != utcKernelCr3 && cpu->cs == 0x23, "real private CPL3 caller");
            GtosClockReadResult before, after;
            UtcRequire(tasks.ReadClock(before) == 0, "independent coherent monotonic clock");
            const uint32_t free = utcFrames->getStatistics().freeFrames;
            UtcRecord record;
            UtcRequire(NativeClockFixture::CopyCurrent(runtime, &record, GTOS_UTC_RECORD_VA, sizeof(record)), "actual method marker");
            uint32_t output = 0;
            uint8_t previous[48], result[48];
            if (cpu->eax == GTOS_SYS_REALTIME_READ) {
                GtosRealtimeReadRequest request;
                UtcRequire(cpu->ecx == sizeof(request) && NativeClockFixture::CopyCurrent(runtime, &request, cpu->ebx, sizeof(request))
                    && request.version == 1 && !request.flags && request.result_bytes == 48, "actual UTC bridge request");
                output = request.result_va;
                ++utcCalls;
            } else {
                GtosClockReadRequest request;
                UtcRequire(cpu->ecx == sizeof(request) && NativeClockFixture::CopyCurrent(runtime, &request, cpu->ebx, sizeof(request))
                    && request.version == 1 && request.clock_id == GTOS_CLOCK_ID_MONOTONIC && !request.flags
                    && request.result_bytes == 48, "unchanged actual monotonic bridge request");
                output = request.result_va;
                ++monoCalls;
            }
            UtcRequire(NativeClockFixture::CopyCurrent(runtime, previous, output, 48), "whole pre-call output snapshot");
            const uint32_t returned = original.HandlerInterrupt(esp);
            UtcRequire(returned == esp && tasks.ReadClock(after) == 0 && before.microseconds == after.microseconds
                && before.delivered_ticks == after.delivered_ticks && utcIrqs == after.delivered_ticks
                && utcFrames->getStatistics().freeFrames == free, "clock call cannot schedule, invent ticks or allocate");
            UtcRequire(cpu->ebx == registers.ebx && cpu->ecx == registers.ecx && cpu->edx == registers.edx
                && cpu->esi == registers.esi && cpu->edi == registers.edi && cpu->ebp == registers.ebp
                && cpu->eip == registers.eip && cpu->esp == registers.esp && cpu->cs == registers.cs
                && cpu->ss == registers.ss && cpu->eflags == registers.eflags, "non-result user frame preserved");
            UtcRequire(NativeClockFixture::CopyCurrent(runtime, result, output, 48), "whole post-call output snapshot");
            uint64_t value = 0, mono = before.microseconds, ticks = before.delivered_ticks;
            if (cpu->eax) {
                ++errors;
                UtcRequire(GTOS_UTC_UNSUPPORTED && registers.eax == GTOS_SYS_REALTIME_READ
                    && (int32_t)cpu->eax == -38, "actual missing RTC, no epoch fallback");
                for (uint32_t i = 0; i < 48; ++i) UtcRequire(previous[i] == result[i], "unavailable UTC preserves whole output");
            } else if (registers.eax == GTOS_SYS_REALTIME_READ) {
                GtosRealtimeReadResult snapshot;
                for (uint32_t i = 0; i < 48; ++i) ((uint8_t*)&snapshot)[i] = result[i];
                const GtosRealtimeReadResult* r = &snapshot;
                UtcRequire(r->version == 1 && r->unit == 1 && r->source == 1 && r->capabilities == 15
                    && r->resolution_us == 10000 && r->anchor_uncertainty_us == 1000000
                    && r->monotonic_microseconds == mono && r->delivered_ticks == ticks
                    && r->microseconds >= mono, "real UTC calendar anchor and independently counted IRQs");
                value = r->microseconds;
                if (!anchored) { utcAnchor = value - mono; anchored = true; }
                UtcRequire(value - mono == utcAnchor, "immutable real hardware epoch anchor");
            } else {
                GtosClockReadResult snapshot;
                for (uint32_t i = 0; i < 48; ++i) ((uint8_t*)&snapshot)[i] = result[i];
                const GtosClockReadResult* r = &snapshot;
                UtcRequire(r->version == 1 && r->clock_id == GTOS_CLOCK_ID_MONOTONIC
                    && r->unit == 1 && r->source == 1 && r->capabilities == 7 && r->resolution_us == 10000
                    && r->pit_input_hz == 1193182 && r->pit_divisor == 11931
                    && r->microseconds == mono && r->delivered_ticks == ticks, "original TimeTicks actual snapshot");
                value = mono;
            }
            if (record.pending_kind <= 2) {
                UtcRequire(record.sample_count < 8 && !events[record.sample_count].present
                    && ((record.pending_kind == 0) == (registers.eax == GTOS_SYS_CLOCK_READ)), "one syscall per genuine V8 method");
                Event& e = events[record.sample_count];
                e.kind = record.pending_kind; e.present = 1; e.error = cpu->eax; e.value = value; e.mono = mono; e.ticks = ticks;
            } else UtcRequire(record.pending_kind == 9, "bounded raw-validation/wait phase");
            return returned;
        }
    };
}
asm(".section .text.native_utc_peer,\"ax\"\n.balign 16\n"
    ".global native_user_start,native_user_end\nnative_user_start:\n"
    "incl 0x4000200c\njmp native_user_start\nnative_user_end:\n.text\n");
extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    printf((char*)"NATIVE V8 UTC SMOKE BOOT\n");
    GlobalDescriptorTable gdt; TaskManager tasks; InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80); PhysicalMemoryManager frames;
    UtcRequire(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "initialize frames");
    utcFrames = &frames; KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end, (uint32_t)&kernel_readonly_start,
        (uint32_t)&kernel_readonly_end, (const MultibootInfo*)multiboot, 0, 0};
    UtcRequire(paging.prepareIdentity(frames, config), "identity paging");
    NativeRuntime runtime;
    UtcRequire(runtime.PrepareStacks(paging, frames) && paging.enable() && paging.sealForSharedProcessors(), "guarded sealed paging");
    NativeRtcSnapshot before, after;
    UtcRequire(ReadNativeRtc(before), "actual RTC before activation"); UtcRtc("before", before);
    Port8Bit index(0x70), data(0x71);
    if (GTOS_UTC_UNSUPPORTED) { index.Port8Bit::Write(0x0B); data.Port8Bit::Write(before.format | 1); }
    UtcRequire(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2)
        && runtime.FpEnabled() && runtime.FpError() == NativeFpOk, "activate real production SSE2/x87 runtime");
    if (GTOS_UTC_UNSUPPORTED) { index.Port8Bit::Write(0x0B); data.Port8Bit::Write(before.format); }
    UtcRequire(ReadNativeRtc(after), "test restores original CMOS immediately"); UtcRtc("after", after);
    utcKernelCr3 = paging.getStatistics().directoryAddress;
    UtcSyscallTap syscallTap(interrupts, runtime, tasks, syscalls); UtcTimerTap timerTap(interrupts);
    const uint32_t initial = frames.getStatistics().freeFrames;
    const MultibootInfo* boot = (const MultibootInfo*)multiboot;
    UtcRequire((boot->flags & 8) && boot->moduleCount == 4, "four actual LLVM ELF modes");
    const MultibootModule* modules = (const MultibootModule*)boot->modules;
    Task ring0(&gdt, Ring0Task); UtcRequire(tasks.AddTask(&ring0), "ring0 peer");
    const uint32_t peer = Create(runtime, 0x11223344, 0);
    asm volatile("outb %0,$0x43" : : "a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR & 255)));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR >> 8)));
    interrupts.Activate(); WaitTicks(tasks, 8);
    const uint32_t survivor = frames.getStatistics().freeFrames;
    UtcRequire(initial - survivor == 7, "independent raw peer cost");
    for (uint32_t iteration = 0; iteration < (GTOS_UTC_UNSUPPORTED ? 1U : 4U); ++iteration) {
        const uint32_t mode = GTOS_UTC_UNSUPPORTED ? 3U : (iteration == 3 ? 0U : iteration);
        uint32_t peerBefore[4]; UtcRequire(runtime.ReadMemory(peer, NativeRuntime::DataAddress, peerBefore, sizeof(peerBefore)), "raw peer snapshot");
        const uint32_t ringBefore = ring0Progress, bootBefore = tasks.BootTicks();
        const uint32_t bytes = modules[mode].end - modules[mode].start;
        Elf32LoadPlan plan; UtcRequire(modules[mode].end > modules[mode].start && bytes <= 65536
            && ValidateElf32((const uint8_t*)modules[mode].start, bytes, plan), "actual bounded ELF32 admission");
        const uint32_t cost = UtcCost(plan);
        const NativeFpStatistics beforeFp = runtime.FpStatistics();
        {
            InterruptGuard guard;
            for (uint32_t i = 0; i < 8; ++i) events[i] = Event();
            utcCalls = monoCalls = errors = 0;
            UtcRequire(runtime.CreateElf((const uint8_t*)modules[mode].start, bytes, utcId), "native V8 UTC ELF admission");
        }
        if (mode == 2) {
            const uint32_t start = tasks.Ticks();
            for (;;) {
                UtcRecord record; NativeStatus status;
                { InterruptGuard guard; record = UtcRead(runtime, utcId); UtcRequire(runtime.Status(utcId, status), "waiting caller"); }
                if (record.stage == 2) break;
                UtcRequire(status.live && tasks.Ticks() - start < 1000, "bounded completed cancellation phase"); WaitTicks(tasks, 1);
            }
            UtcRequire(runtime.RequestExit(utcId, 73), "actual external close");
        }
        const NativeStatus status = WaitStopped(runtime, tasks, utcId);
        const UtcRecord record = UtcRead(runtime, utcId);
        UtcRequire(record.version == 1 && record.mode == mode && !record.error && record.checks
            && record.base == 0x80000000U && record.handle && record.keep_pages == 2
            && status.observedCs == 0x23 && status.observedCr3 == status.directory && status.directory != utcKernelCr3,
            "actual V8 record, private CPL3 and retained ownership");
        UtcRequire(runtime.ReadMemory(utcId, record.base, retained, sizeof(retained)), "retained full 8192 bytes");
        for (uint32_t i = 0; i < sizeof(retained); ++i) UtcRequire(retained[i] == (uint8_t)(i ^ 0x5D), "no clock or stop corrupts retained pages");
        UtcRequire(survivor - frames.getStatistics().freeFrames == cost + 3 && !status.live && !status.reaped, "exact stopped frame cost");
        if (mode == 3) UtcRequire(record.stage == 1 && record.sample_count == 1 && errors == 1 && utcCalls == 1
            && monoCalls == 1 && status.faultVector == 6 && status.exitCode == 0x80000006U
            && !status.faultError && !status.faultAddress && events[1].present && events[1].error == (uint32_t)-38, "real missing RTC takes bridge fatal UD2 after valid TimeTicks");
        else {
            UtcRequire(record.stage == 2 && record.sample_count == 8 && record.rejected == 22
                && record.sentinel_checks == 8192 && !errors && utcCalls == 5 && monoCalls >= 4,
                "actual positive V8 UTC/TimeTicks and signed-domain validator");
            UtcRequire(record.samples[4].value > record.samples[0].value, "real delivered IRQ progress");
            if (mode == 1) UtcRequire(status.faultVector == 14 && status.faultError == 6
                && status.faultAddress == 0xBFFFCFFCU && status.exitCode == 0x8000000EU, "exact contained writable guard fault");
            else UtcRequire(!status.faultVector && status.exitCode == (mode == 2 ? 73U : 0U), "normal exit or external cancellation");
        }
        for (uint32_t i = 0; i < record.sample_count; ++i) {
            const Event& e = events[i];
            UtcRequire(e.present && !e.error && record.samples[i].kind == e.kind && !record.samples[i].reserved
                && record.samples[i].value == e.value + (e.kind == 0 ? 1 : 0), "genuine method result equals independently captured syscall payload");
        }
        {
            InterruptGuard guard;
            printf((char*)"UTC RECORD words=");
            const uint8_t* raw = (const uint8_t*)&record;
            for (uint32_t i = 0; i < sizeof(record); i += 4) { printf((char*)" "); printfHex32(Get32(raw + i)); }
            printf((char*)"\n");
            for (uint32_t i = 0; i < 8; ++i) if (events[i].present) {
                const Event& e = events[i]; printf((char*)"UTC EVENT index="); printfHex32(i);
                printf((char*)" kind="); printfHex32(e.kind); printf((char*)" error="); printfHex32(e.error);
                printf((char*)" value="); UtcHex64(e.value); printf((char*)" mono="); UtcHex64(e.mono);
                printf((char*)" ticks="); UtcHex64(e.ticks); printf((char*)"\n");
            }
            printf((char*)"UTC CASE mode="); printfHex32(mode); printf((char*)" id="); printfHex32(utcId);
            printf((char*)" utc="); printfHex32(utcCalls); printf((char*)" mono="); printfHex32(monoCalls);
            printf((char*)" errors="); printfHex32(errors); printf((char*)" cost="); printfHex32(cost);
            printf((char*)" dynamic=00000003 exit="); printfHex32(status.exitCode);
            printf((char*)" vector="); printfHex32(status.faultVector); printf((char*)" pf="); printfHex32(status.faultError);
            printf((char*)" cr2="); printfHex32(status.faultAddress); printf((char*)" cs="); printfHex32(status.observedCs);
            printf((char*)" cr3="); printfHex32(status.observedCr3); printf((char*)" kernel_cr3="); printfHex32(utcKernelCr3);
            printf((char*)"\n");
        }
        UtcRequire(runtime.Reap() == 1 && frames.getStatistics().freeFrames == survivor, "exact deferred victim Reap");
        const NativeFpStatistics afterFp = runtime.FpStatistics();
        UtcRequire(afterFp.initialized == beforeFp.initialized + 1 && afterFp.invalidated == beforeFp.invalidated + 1
            && afterFp.saves > beforeFp.saves && afterFp.restores > beforeFp.restores && !afterFp.invariantFailures,
            "actual FP owner initialization, transitions and invalidation on Reap");
        WaitTicks(tasks, 8);
        uint32_t peerAfter[4]; NativeStatus peerStatus;
        UtcRequire(runtime.ReadMemory(peer, NativeRuntime::DataAddress, peerAfter, sizeof(peerAfter))
            && runtime.Status(peer, peerStatus) && peerStatus.live && !peerStatus.systemCalls && peerAfter[3] != peerBefore[3]
            && peerAfter[0] == 0x11223344 && ring0Progress != ringBefore && tasks.BootTicks() > bootBefore,
            "CPU-bound raw peer, ring0 and boot continue after each Reap");
        { InterruptGuard guard; printf((char*)"UTC REAP free="); printfHex32(survivor);
          printf((char*)" expected="); printfHex32(frames.getStatistics().freeFrames); printf((char*)" peers=00000001 fp_initialized="); printfHex32(afterFp.initialized);
          printf((char*)" fp_invalidated="); printfHex32(afterFp.invalidated);
          printf((char*)" fp_saves="); printfHex32(afterFp.saves);
          printf((char*)" fp_restores="); printfHex32(afterFp.restores);
          printf((char*)" fp_failures="); printfHex32(afterFp.invariantFailures); printf((char*)"\n"); }
    }
    UtcRequire(runtime.RequestExit(peer, 0) && runtime.Reap() == 1 && frames.getStatistics().freeFrames == initial, "exact original allocator baseline");
    const NativeFpStatistics finalFp = runtime.FpStatistics();
    UtcRequire(finalFp.initialized == (GTOS_UTC_UNSUPPORTED ? 2U : 5U)
        && finalFp.invalidated == finalFp.initialized && !finalFp.invariantFailures, "all actual FP owners reclaimed");
    { InterruptGuard guard; printf((char*)"UTC FINAL free="); printfHex32(frames.getStatistics().freeFrames);
      printf((char*)" expected="); printfHex32(initial); printf((char*)" anchor="); UtcHex64(utcAnchor); printf((char*)" fp_initialized="); printfHex32(finalFp.initialized);
      printf((char*)" fp_invalidated="); printfHex32(finalFp.invalidated);
      printf((char*)" fp_failures="); printfHex32(finalFp.invariantFailures); printf((char*)"\n"); UtcFinish(true); }
}
