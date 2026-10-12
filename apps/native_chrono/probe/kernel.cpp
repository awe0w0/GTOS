#define NativeProcessSmoke NativeChronoUnusedBaseline
#include "tests/native_process_smoke.cpp"
#undef NativeProcessSmoke
#include "record.h"
#include "profile-values.h"
#include <memory/criticalsection.h>

namespace {
    uint8_t chronoElf[1048576];
    const uint32_t RecordAddress = 0x40100000U;
    void ChronoFinish(bool pass) {
        printf(pass ? (char*)"NATIVE CHRONO SMOKE PASS\n" : (char*)"NATIVE CHRONO SMOKE FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void ChronoRequire(bool value, const char* why) {
        if (!value) { printf((char*)"FAILED CHRONO "); printf((char*)why); printf((char*)"\n"); ChronoFinish(false); }
    }
    NativeChronoRecord Read(NativeRuntime& runtime, uint32_t id) {
        NativeChronoRecord record = {};
        ChronoRequire(runtime.ReadMemory(id, RecordAddress, &record, sizeof(record)), "full CPL3 record");
        return record;
    }
    uint32_t Cost(const Elf32LoadPlan& plan) {
        uint32_t seen[32] = {}, tables = 0;
        for (uint32_t i = 0; i < plan.segmentCount; ++i) {
            const Elf32LoadSegment& segment = plan.segments[i];
            if (!segment.memorySize) continue;
            const uint32_t last = (segment.virtualAddress + segment.memorySize - 1) >> 22;
            for (uint32_t index = segment.virtualAddress >> 22; index <= last; ++index) {
                const uint32_t bit = 1U << (index & 31);
                if (!(seen[index >> 5] & bit)) { seen[index >> 5] |= bit; ++tables; }
            }
        }
        const uint32_t stack = NativeRuntime::UserStackBottom >> 22;
        if (!(seen[stack >> 5] & (1U << (stack & 31)))) ++tables;
        ChronoRequire(!(seen[0x80000000U >> 27] & (1U << ((0x80000000U >> 22) & 31))), "dynamic table absent in ELF");
        return plan.pageCount + 2 + tables + 1;
    }
    uint32_t Spawn(NativeRuntime& runtime, const MultibootModule& module, uint32_t mode, uint32_t& cost) {
        ChronoRequire(module.end > module.start && module.end - module.start <= sizeof(chronoElf), "bounded ELF input");
        const uint32_t bytes = module.end - module.start;
        Elf32LoadPlan plan;
        ChronoRequire(ValidateElf32((const uint8_t*)module.start, bytes, plan), "real ELF validation");
        for (uint32_t i = 0; i < bytes; ++i) chronoElf[i] = ((const uint8_t*)module.start)[i];
        bool found = false;
        for (uint32_t i = 0; i < plan.segmentCount; ++i) {
            const Elf32LoadSegment& segment = plan.segments[i];
            if (RecordAddress < segment.virtualAddress || RecordAddress - segment.virtualAddress > segment.fileSize
                || segment.fileSize - (RecordAddress - segment.virtualAddress) < sizeof(NativeChronoRecord)) continue;
            uint8_t* record = chronoElf + segment.fileOffset + RecordAddress - segment.virtualAddress;
            ChronoRequire(Get32(record) == 1 && Get32(record + 4) == 0, "fresh initial state");
            Put32(record + 4, mode); found = true;
        }
        ChronoRequire(found, "file backed record");
        uint32_t id = 0;
        ChronoRequire(runtime.CreateElf(chronoElf, bytes, id) && id, "production CPL3 admission");
        cost = Cost(plan); return id;
    }
    NativeStatus Wait(NativeRuntime& runtime, TaskManager& tasks, uint32_t id, uint32_t mode) {
        const uint32_t begin = tasks.Ticks();
        for (;;) {
            NativeStatus status = {};
            ChronoRequire(runtime.Status(id, status), "status retained");
            if (!status.live) return status;
            if (mode == 2 && Read(runtime, id).cycles >= 3) {
                ChronoRequire(runtime.RequestExit(id, 73), "cancel pending callback");
                ChronoRequire(runtime.Status(id, status) && !status.live, "cancel stopped");
                return status;
            }
            ChronoRequire((uint32_t)(tasks.Ticks() - begin) < 2000, "bounded guest deadline");
            WaitTicks(tasks, 1);
        }
    }
}
namespace {
    struct ChronoEvent { uint32_t kind; uint64_t value; } chronoEvents[4];
    uint32_t chronoId, chronoMonoCalls, chronoWallCalls, chronoErrors;
    PhysicalMemoryManager* chronoFrames;
    uint64_t chronoAnchor;
    bool chronoHaveAnchor;
}
namespace gtos {
    struct NativeClockFixture {
        static bool CopyCurrent(NativeRuntime& runtime, void* out, uint32_t address, uint32_t bytes) {
            NativeRuntime::Slot* slot = runtime.Current();
            return slot && slot->space.CopyFromUser(out, address, bytes);
        }
        // Test-only failure injection while the hardware IRQ gate is clear.
        // No tick/time seed or production API is changed; restore before IRQs.
        static bool Exhausted(TaskManager& tasks, bool value) {
            const bool previous = tasks.clock.exhausted;
            tasks.clock.exhausted = value;
            return previous;
        }
    };
}
namespace {
    class ChronoSyscallTap : public InterruptHandler {
        NativeRuntime& runtime; TaskManager& tasks; SyscallHandler& original;
    public:
        ChronoSyscallTap(InterruptsManager& interrupts, NativeRuntime& r, TaskManager& t, SyscallHandler& o)
            : InterruptHandler(&interrupts, 0x80), runtime(r), tasks(t), original(o) {}
        uint32_t HandlerInterrupt(uint32_t esp) {
            CPUState* cpu = (CPUState*)esp;
            if (cpu->eax != GTOS_SYS_CLOCK_READ && cpu->eax != GTOS_SYS_REALTIME_READ)
                return original.HandlerInterrupt(esp);
            const CPUState registers = *cpu;
            NativeChronoRecord record;
            ChronoRequire(NativeClockFixture::CopyCurrent(runtime, &record, RecordAddress, sizeof(record)), "actual chrono method marker");
            NativeStatus status = {};
            ChronoRequire(runtime.Status(chronoId, status) && status.live && cpu->cs == 0x23,
                "actual private CPL3 clock caller");
            GtosClockReadResult before = {}, after = {};
            ChronoRequire(tasks.ReadClock(before) == 0, "independent coherent PIT snapshot");
            const uint32_t free = chronoFrames->getStatistics().freeFrames;
            uint32_t output = 0;
            if (cpu->eax == GTOS_SYS_CLOCK_READ) {
                GtosClockReadRequest request = {};
                ChronoRequire(cpu->ecx == sizeof(request) && NativeClockFixture::CopyCurrent(runtime, &request, cpu->ebx, sizeof(request))
                    && request.version == 1 && request.clock_id == 1 && !request.flags && request.result_bytes == 48, "real monotonic request");
                output = request.result_va; ++chronoMonoCalls;
            } else {
                GtosRealtimeReadRequest request = {};
                ChronoRequire(cpu->ecx == sizeof(request) && NativeClockFixture::CopyCurrent(runtime, &request, cpu->ebx, sizeof(request))
                    && request.version == 1 && !request.flags && request.result_bytes == 48, "real UTC request");
                output = request.result_va; ++chronoWallCalls;
            }
            uint8_t previous[48], actual[48];
            ChronoRequire(NativeClockFixture::CopyCurrent(runtime, previous, output, 48), "complete pre-call buffer");
            const bool inject = record.pending == 4;
            if (inject) ChronoRequire(record.mode == 3 && record.calls == 5 && record.mono == 2
                && record.wall == 2 && !chronoErrors && cpu->eax == GTOS_SYS_CLOCK_READ, "single actual chrono error path");
            bool old = false;
            if (inject) old = NativeClockFixture::Exhausted(tasks, true);
            const uint32_t returned = original.HandlerInterrupt(esp);
            if (inject) NativeClockFixture::Exhausted(tasks, old);
            ChronoRequire(returned == esp && tasks.ReadClock(after) == 0
                && before.microseconds == after.microseconds && before.delivered_ticks == after.delivered_ticks
                && chronoFrames->getStatistics().freeFrames == free, "clock cannot schedule or allocate; producer restored");
            ChronoRequire(cpu->ebx == registers.ebx && cpu->ecx == registers.ecx && cpu->edx == registers.edx
                && cpu->esi == registers.esi && cpu->edi == registers.edi && cpu->ebp == registers.ebp
                && cpu->eip == registers.eip && cpu->esp == registers.esp && cpu->cs == registers.cs
                && cpu->ss == registers.ss && cpu->eflags == registers.eflags, "non-result registers preserved");
            ChronoRequire(NativeClockFixture::CopyCurrent(runtime, actual, output, 48), "complete post-call buffer");
            if (inject) {
                ChronoRequire(!old && (int32_t)cpu->eax == GTOS_CLOCK_ERR_OVERFLOW, "real production exhausted-clock failure");
                for (uint32_t i = 0; i < 48; ++i) ChronoRequire(actual[i] == previous[i], "failure preserves output");
                ++chronoErrors;
                return returned;
            }
            ChronoRequire(cpu->eax == 0, "actual clock service available");
            uint64_t value;
            if (registers.eax == GTOS_SYS_CLOCK_READ) {
                GtosClockReadResult result = {};
                for (uint32_t i = 0; i < 48; ++i) ((uint8_t*)&result)[i] = actual[i];
                ChronoRequire(result.version == 1 && result.clock_id == 1 && result.unit == 1 && result.source == 1
                    && result.capabilities == 7 && result.resolution_us == 10000
                    && result.microseconds == before.microseconds && result.delivered_ticks == before.delivered_ticks,
                    "real PIT source and coherent microseconds");
                value = result.microseconds * 1000U;
            } else {
                GtosRealtimeReadResult result = {};
                for (uint32_t i = 0; i < 48; ++i) ((uint8_t*)&result)[i] = actual[i];
                ChronoRequire(result.version == 1 && result.unit == 1 && result.source == 1 && result.capabilities == 15
                    && result.microseconds >= before.microseconds && result.monotonic_microseconds == before.microseconds
                    && result.delivered_ticks == before.delivered_ticks, "real RTC anchored wall clock");
                value = result.microseconds;
                if (!chronoHaveAnchor) { chronoAnchor = value - before.microseconds; chronoHaveAnchor = true; }
                ChronoRequire(value - before.microseconds == chronoAnchor, "RTC anchor never changes with process lifetime");
            }
            if (record.pending == 1 || record.pending == 2) {
                const uint32_t index = record.mono + record.wall;
                ChronoRequire(index < 4 && !chronoEvents[index].kind
                    && ((record.pending == 1) == (registers.eax == GTOS_SYS_CLOCK_READ)), "one real service per chrono call");
                chronoEvents[index].kind = record.pending;
                chronoEvents[index].value = value;
            } else ChronoRequire(record.pending == 9, "bounded raw wait or cancel/fault clock phase");
            return returned;
        }
    };
}

asm(".section .text.native_chrono_integer_peer,\"ax\"\n.balign 16\n"
    ".global native_user_start,native_user_end\nnative_user_start:\n"
    "incl 0x4000200c\njmp native_user_start\nnative_user_end:\n.text\n");
extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    printf((char*)"NATIVE CHRONO SMOKE BOOT\n");
    GlobalDescriptorTable gdt;
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    ChronoRequire(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frame bootstrap");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end, (const MultibootInfo*)multiboot, 0, 0};
    ChronoRequire(paging.prepareIdentity(frames, config), "identity template");
    NativeRuntime runtime;
    ChronoRequire(runtime.PrepareStacks(paging, frames), "guarded kernel stacks");
    ChronoRequire(paging.enable() && paging.sealForSharedProcessors(), "sealed template");
    ChronoRequire(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2), "native runtime with user SSE2");
    chronoFrames = &frames;
    ChronoSyscallTap tap(interrupts, runtime, tasks, syscalls);
    const uint32_t kernelDirectory = paging.getStatistics().directoryAddress;
    const uint32_t baseline = frames.getStatistics().freeFrames;
    Task ring0(&gdt, Ring0Task);
    ChronoRequire(tasks.AddTask(&ring0), "ring0 survivor");
    const MultibootInfo* boot = (const MultibootInfo*)multiboot;
    ChronoRequire((boot->flags & 8) && boot->moduleCount == 1, "one actual native ELF");
    const MultibootModule* modules = (const MultibootModule*)boot->modules;
    const uint32_t peer = Create(runtime, 0x11223344U, 0);
    const uint32_t survivorBaseline = frames.getStatistics().freeFrames;
    ChronoRequire(baseline - survivorBaseline == 7, "peer frame cost");
    asm volatile("outb %0,$0x43" : : "a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(11931 & 255)));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(11931 >> 8)));
    interrupts.Activate();
    const uint32_t modes[] = {0,1,2,3,0};
    for (uint32_t n = 0; n < sizeof(modes) / 4; ++n) {
        uint32_t beforePeer[4], afterPeer[4];
        ChronoRequire(runtime.ReadMemory(peer, NativeRuntime::DataAddress, beforePeer, sizeof(beforePeer)), "peer before");
        const uint32_t beforeRing0 = ring0Progress, beforeBoot = tasks.BootTicks();
        uint32_t cost = 0;
        uint32_t id = 0;
        {
            InterruptGuard guard;
            chronoMonoCalls = chronoWallCalls = chronoErrors = 0;
            for (uint32_t i = 0; i < 4; ++i) { chronoEvents[i].kind = 0; chronoEvents[i].value = 0; }
            id = Spawn(runtime, modules[0], modes[n], cost);
            chronoId = id;
        }
        const NativeStatus status = Wait(runtime, tasks, id, modes[n]);
        const NativeChronoRecord record = Read(runtime, id);
        ChronoRequire(status.observedCs == 0x23 && status.observedCr3 == status.directory
            && status.directory != kernelDirectory, "observed private CPL3");
        printf((char*)"\nCHRONO CASE id="); printfHex32(id); printf((char*)" mode="); printfHex32(modes[n]);
        printf((char*)" cs="); printfHex32(status.observedCs); printf((char*)" cr3="); printfHex32(status.directory);
        printf((char*)" exit="); printfHex32(status.exitCode); printf((char*)" vector="); printfHex32(status.faultVector);
        printf((char*)" cost="); printfHex32(cost + 2); printf((char*)" words= ");
        const uint32_t* words = (const uint32_t*)&record;
        for (uint32_t i = 0; i < 16; ++i) { printfHex32(words[i]); printf((char*)" "); }
        printf((char*)"\n");
        ChronoRequire(record.version == 1 && record.mode == modes[n] && record.error == 0
            && record.checks >= 5 && record.region && record.base == 0x80000000U
            && record.length == 4096 && record.pending <= 9, "validated record and resident region");
        uint32_t marker = 0;
        ChronoRequire(runtime.ReadMemory(id, record.base, &marker, sizeof(marker)) && marker == 0xC011CA11U, "callback resources retained");
        uint64_t data[4] = {};
        ChronoRequire(record.pointer >= RecordAddress && record.objectBytes == 32 && record.heapEmpty == 1
            && runtime.ReadMemory(id, record.pointer, data, sizeof(data)), "actual chrono values retained without heap");
        for (uint32_t i = 0; i < 4; ++i) ChronoRequire(chronoEvents[i].kind == (i % 2 ? 2U : 1U)
            && data[i] == chronoEvents[i].value, "exact real syscall result and chrono unit conversion");
        ChronoRequire(data[2] - data[0] >= 30000000U && data[3] - data[1] >= 30000U,
            "real elapsed PIT and wall clock advances");
        printf((char*)"CHRONO VALUES id="); printfHex32(id); printf((char*)" mono_calls="); printfHex32(chronoMonoCalls);
        printf((char*)" wall_calls="); printfHex32(chronoWallCalls); printf((char*)" errors="); printfHex32(chronoErrors);
        printf((char*)" values= ");
        for (uint32_t i = 0; i < 4; ++i) { printfHex32(data[i] >> 32); printfHex32(data[i]); printf((char*)" "); }
        printf((char*)"\n");
        ChronoRequire(record.stage == 2 && record.checks >= 19 && record.mono == 2 && record.wall == 2
            && chronoMonoCalls >= 4 && chronoWallCalls == 2, "actual clock consumers completed");
        if (modes[n] == 0) ChronoRequire(record.calls == 4 && !record.cycles && !record.pending
            && !chronoErrors && !status.exitCode && !status.faultVector, "real positive chrono completion");
        else if (modes[n] == 1) ChronoRequire(record.calls == 4 && !record.cycles && record.pending == 9
            && !chronoErrors && status.exitCode == (0x80000000U | 14) && status.faultVector == 14
            && (status.faultError & 6) == 6 && status.faultAddress == 0xBFFFCFFCU, "invalid clock output write contained");
        else if (modes[n] == 2) ChronoRequire(record.calls >= 7 && record.cycles >= 3 && record.pending == 9
            && !chronoErrors && status.exitCode == 73 && !status.faultVector, "cancel actual repeated chrono calls");
        else ChronoRequire(record.calls == 5 && !record.cycles && record.pending == 4 && chronoErrors == 1
            && status.exitCode == ExpectedChronoAbortExit && !status.faultVector, "real exhausted-clock error terminates without time fallback");
        ChronoRequire(survivorBaseline - frames.getStatistics().freeFrames == cost + 2, "ELF plus one resident page and one table retained");
        ChronoRequire(runtime.Reap() == 1 && frames.getStatistics().freeFrames == survivorBaseline, "exact victim reap");
        WaitTicks(tasks, 6);
        NativeStatus peerStatus = {};
        ChronoRequire(runtime.Status(peer, peerStatus) && peerStatus.live && !peerStatus.systemCalls
            && runtime.ReadMemory(peer, NativeRuntime::DataAddress, afterPeer, sizeof(afterPeer))
            && afterPeer[3] != beforePeer[3] && ring0Progress != beforeRing0 && tasks.BootTicks() > beforeBoot,
            "integer ring0 and boot peers survive");
        printf((char*)"CHRONO REAP free="); printfHex32(frames.getStatistics().freeFrames);
        printf((char*)" expected="); printfHex32(survivorBaseline); printf((char*)"\n");
    }
    ChronoRequire(runtime.RequestExit(peer, 0) && runtime.Reap() == 1
        && frames.getStatistics().freeFrames == baseline, "exact final baseline");
    ChronoRequire(!runtime.FpStatistics().invariantFailures, "FP ownership preserved");
    printf((char*)"CHRONO FINAL free="); printfHex32(frames.getStatistics().freeFrames);
    printf((char*)" expected="); printfHex32(baseline); printf((char*)"\n");
    ChronoFinish(true);
}
