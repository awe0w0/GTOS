// The old baseline supplies boot helpers only; its entry is discarded by GC.
#define NativeProcessSmoke NativeClockUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include <process/clock_abi.h>
#include <memory/criticalsection.h>
#include "../apps/native_clock_probe/record.h"
#include "clock_seed.h"

namespace {
    volatile uint64_t observedIrqs;
    volatile uint32_t deliveredIrqs, clockCalls, legacyCalls, clockErrors, preservedCalls;
    uint32_t clockPeerId, kernelDirectory, survivorFrames;
    PhysicalMemoryManager* observedFrames;
    uint8_t clockElf[65536], clockPages[8192];
    struct ClockState { uint64_t us, ticks; uint32_t fraction, legacy; bool exhausted; };
    void ClockFinish(bool pass) __attribute__((noreturn));
    void ClockFinish(bool pass) {
        printf(pass ? (char*)"NATIVE CLOCK SMOKE PASS\n" : (char*)"NATIVE CLOCK SMOKE FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void ClockRequire(bool condition, const char* why) {
        if (!condition) { printf((char*)"FAILED CLOCK "); printf((char*)why); printf((char*)"\n"); ClockFinish(false); }
    }
    uint32_t ClockFlags() { uint32_t flags; asm volatile("pushfl; popl %0" : "=r"(flags)); return flags; }
    void ClockHex64(uint64_t value) { printfHex32((uint32_t)(value >> 32)); printfHex32((uint32_t)value); }
    void ClockPayload(const GtosClockReadResult& value) {
        const uint8_t* bytes = (const uint8_t*)&value;
        for (uint32_t i = 0; i < 12; ++i) { printf((char*)" "); printfHex32(Get32(bytes + i * 4)); }
    }
    bool ClockSame(const ClockState& a, const ClockState& b) {
        return a.us == b.us && a.ticks == b.ticks && a.fraction == b.fraction
            && a.legacy == b.legacy && a.exhausted == b.exhausted;
    }
    bool ClockMetadata(const GtosClockReadResult& value) {
        return value.version == 1 && value.clock_id == 1 && value.unit == 1 && value.source == 1
            && value.capabilities == 7 && value.resolution_us == 10000
            && value.pit_input_hz == 1193182 && value.pit_divisor == 11931;
    }
    uint64_t ClockObserved() { InterruptGuard guard; return observedIrqs; }
    void ClockA5(void* bytes, uint32_t size) { for (uint32_t i = 0; i < size; ++i) ((uint8_t*)bytes)[i] = 0xA5; }
    bool ClockSentinel(const void* bytes, uint32_t size) {
        for (uint32_t i = 0; i < size; ++i) if (((const uint8_t*)bytes)[i] != 0xA5) return false;
        return true;
    }
}
namespace gtos {
    // This fixture exists only in this acceptance ELF. No production seed API.
    struct NativeClockFixture {
        static ClockState State(const TaskManager& tasks) {
            const ClockState state = {tasks.clock.microseconds, tasks.clock.deliveredTicks,
                tasks.clock.fraction, tasks.ticks, tasks.clock.exhausted}; return state;
        }
        static void Seed(TaskManager& tasks) {
            ClockRequire(!(ClockFlags() & 0x200) && !deliveredIrqs && !tasks.numTasks,
                "consistent clock seed only before PIC/user dispatch");
            tasks.clock.microseconds = CLOCK_SEED_US;
            tasks.clock.deliveredTicks = CLOCK_SEED_TICKS;
            tasks.clock.fraction = CLOCK_SEED_FRACTION;
            tasks.clock.exhausted = false;
            tasks.ticks = (uint32_t)CLOCK_SEED_TICKS;
        }
        static void PrivateDispatch() {
            TaskManager privateTasks; Seed(privateTasks);
            CPUState privateFrame = {};
            const ClockState before = State(privateTasks);
            ClockRequire(privateTasks.Dispatch(&privateFrame, false) == &privateFrame
                && ClockSame(before, State(privateTasks)), "private actual Dispatch(false) cannot invent time");
            ClockRequire(!privateTasks.Dispatch(0, true) && ClockSame(before, State(privateTasks)),
                "null timer frame cannot invent time");
            GtosClockReadResult out = {};
            const uint32_t flags = ClockFlags();
            ClockRequire(privateTasks.ReadClock(out) == 0 && (ClockFlags() & 0x200) == (flags & 0x200)
                && out.microseconds == CLOCK_SEED_US && out.delivered_ticks == CLOCK_SEED_TICKS,
                "typed private snapshot preserves IF0 and full coherent seed");
            printf((char*)"CLOCK PRIVATE false_dispatch=00000001 null_timer=00000001 if0=00000001\n");
        }
        static bool CopyCurrent(NativeRuntime& runtime, void* out, uint32_t address, uint32_t bytes) {
            NativeRuntime::Slot* slot = runtime.Current();
            return slot && slot->space.CopyFromUser(out, address, bytes);
        }
    };
}
namespace {
    class ClockTimerTap : public InterruptHandler {
        TaskManager& tasks;
    public:
        ClockTimerTap(InterruptsManager& interrupts, TaskManager& scheduler)
            : InterruptHandler(&interrupts, 0x20), tasks(scheduler) {}
        uint32_t HandlerInterrupt(uint32_t esp) {
            ClockRequire(!(ClockFlags() & 0x200), "hardware IRQ observer enters IF0");
            const ClockState before = NativeClockFixture::State(tasks);
            if (!before.exhausted) ClockRequire(before.ticks == observedIrqs,
                "independent real IRQ observer matches previous producer count");
            ++observedIrqs; ++deliveredIrqs;
            return esp; // The real IRQ manager then calls production Schedule.
        }
    };
    class ClockSyscallTap : public InterruptHandler {
        NativeRuntime& runtime;
        TaskManager& tasks;
        SyscallHandler& original;
    public:
        ClockSyscallTap(InterruptsManager& interrupts, NativeRuntime& current, TaskManager& scheduler, SyscallHandler& handler)
            : InterruptHandler(&interrupts, 0x80), runtime(current), tasks(scheduler), original(handler) {}
        uint32_t HandlerInterrupt(uint32_t esp) {
            CPUState* cpu = (CPUState*)esp;
            const bool clock = cpu->eax == GTOS_SYS_CLOCK_READ;
            const bool legacy = cpu->eax == GTOS_SYS_TICKS;
            if (!clock && !legacy) return original.HandlerInterrupt(esp);
            ClockRequire(!(ClockFlags() & 0x200), "actual clock/TICKS syscall enters IF0");
            const CPUState registers = *cpu;
            const ClockState before = NativeClockFixture::State(tasks);
            const uint64_t independent = observedIrqs;
            const uint32_t framesBefore = observedFrames->getStatistics().freeFrames;
            GtosClockReadRequest request = {};
            const bool copiedRequest = clock && cpu->ecx == sizeof(request)
                && NativeClockFixture::CopyCurrent(runtime, &request, cpu->ebx, sizeof(request));
            const uint32_t returned = original.HandlerInterrupt(esp); // Actual production HandleSyscall.
            const ClockState after = NativeClockFixture::State(tasks);
            ClockRequire(returned == esp && observedIrqs == independent && ClockSame(before, after)
                && observedFrames->getStatistics().freeFrames == framesBefore,
                "read has no timer update, scheduling or physical allocation");
            ClockRequire(cpu->ebx == registers.ebx && cpu->ecx == registers.ecx && cpu->edx == registers.edx
                && cpu->esi == registers.esi && cpu->edi == registers.edi && cpu->ebp == registers.ebp
                && cpu->ds == registers.ds && cpu->es == registers.es && cpu->fs == registers.fs && cpu->gs == registers.gs,
                "actual ABI preserves non-result GPR and segment words");
            ++preservedCalls;
            if (legacy) {
                ++legacyCalls;
                ClockRequire(cpu->eax == (uint32_t)independent, "legacy TICKS returns exact unsigned low32 IRQ bits");
                printf((char*)"CLOCK LEGACY n="); ClockHex64(independent); printf((char*)" eax="); printfHex32(cpu->eax);
                printf((char*)" preserved=00000001 free="); printfHex32(framesBefore); printf((char*)"\n");
            } else {
                ++clockCalls;
                printf((char*)"CLOCK CALL n="); ClockHex64(independent); printf((char*)" status="); printfHex32(cpu->eax);
                printf((char*)" req="); printfHex32(registers.ebx); printf((char*)" bytes="); printfHex32(registers.ecx);
                printf((char*)" copied="); printfHex32(copiedRequest ? 1 : 0);
                if (copiedRequest) {
                    printf((char*)" version="); printfHex32(request.version); printf((char*)" id="); printfHex32(request.clock_id);
                    printf((char*)" flags="); printfHex32(request.flags); printf((char*)" out="); printfHex32(request.result_va);
                    printf((char*)" outbytes="); printfHex32(request.result_bytes);
                }
                if (cpu->eax == 0) {
                    GtosClockReadResult result = {};
                    ClockRequire(copiedRequest && NativeClockFixture::CopyCurrent(runtime, &result, request.result_va, sizeof(result))
                        && ClockMetadata(result) && result.delivered_ticks == independent,
                        "full actual payload matches independently counted same-IRQ snapshot");
                    printf((char*)" payload="); ClockPayload(result);
                } else ++clockErrors;
                printf((char*)" free_before="); printfHex32(framesBefore); printf((char*)" free_after=");
                printfHex32(observedFrames->getStatistics().freeFrames); printf((char*)" preserved=00000001\n");
            }
            return returned;
        }
    };
    ClockProbeRecord ClockRecord(NativeRuntime& runtime, uint32_t id) {
        ClockProbeRecord out = {};
        ClockRequire(runtime.ReadMemory(id, GTOS_CLOCK_PROBE_RECORD_VA, &out, sizeof(out)), "read actual 192B probe record");
        return out;
    }
    void ClockRecordLog(const ClockProbeRecord& record) {
        InterruptGuard logGuard; // Keep the diagnostic record line whole across real preemption.
        printf((char*)"CLOCK RECORD kind="); printfHex32(record.kind); printf((char*)" mode="); printfHex32(record.mode);
        printf((char*)" stage="); printfHex32(record.stage); printf((char*)" checks="); printfHex32(record.checks);
        printf((char*)" scenario="); printfHex32(record.scenario); printf((char*)" error="); printfHex32(record.error);
        printf((char*)" elapsed="); ClockHex64(record.elapsed_us); printf((char*)" legacy_first="); printfHex32(record.legacy_first);
        printf((char*)" legacy_last="); printfHex32(record.legacy_last); printf((char*)" first_status="); printfHex32(record.first_result);
        printf((char*)" last_status="); printfHex32(record.last_result); printf((char*)" sent_bytes="); printfHex32(record.sentinel_bytes);
        printf((char*)" sent_checks="); printfHex32(record.sentinel_checks); printf((char*)" base="); printfHex32(record.keep_base);
        printf((char*)" handle="); printfHex32(record.keep_handle); printf((char*)" pages="); printfHex32(record.keep_pages);
        printf((char*)" before="); printfHex32(record.sentinel_before); printf((char*)" after="); printfHex32(record.sentinel_after);
        printf((char*)" abi="); printfHex32(record.abi_register_checks); printf((char*)" failures="); printfHex32(record.clock_failures);
        printf((char*)"\nCLOCK RECORD RAW words=");
        const uint8_t* raw = (const uint8_t*)&record;
        for (uint32_t i = 0; i < sizeof(record); i += 4) { printf((char*)" "); printfHex32(Get32(raw + i)); }
        printf((char*)"\nCLOCK RECORD FIRST payload="); ClockPayload(record.first);
        printf((char*)"\nCLOCK RECORD LAST payload="); ClockPayload(record.last); printf((char*)"\n");
    }
    ClockProbeRecord ClockStage(NativeRuntime& runtime, TaskManager& tasks, uint32_t id) {
        const uint32_t begin = tasks.Ticks();
        for (;;) {
            const ClockProbeRecord record = ClockRecord(runtime, id);
            if (record.version == 1 && record.stage >= 1) { ClockRecordLog(record); return record; }
            NativeStatus status = {};
            ClockRequire(runtime.Status(id, status) && status.live, "clock probe alive until initialized stage");
            ClockRequire((uint32_t)(tasks.Ticks() - begin) < 500, "bounded clock stage deadline");
            WaitTicks(tasks, 1);
        }
    }
    uint32_t ClockStaticCost(const Elf32LoadPlan& plan) {
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
    uint32_t ClockCreate(NativeRuntime& runtime, const MultibootModule& module, uint32_t& staticCost) {
        const uint32_t bytes = module.end - module.start;
        ClockRequire(module.end > module.start && bytes <= sizeof(clockElf), "bounded clock ELF file");
        Elf32LoadPlan plan;
        ClockRequire(ValidateElf32((const uint8_t*)module.start, bytes, plan) && plan.pageCount <= 254,
            "actual clock ELF32 static admission and total-page budget");
        for (uint32_t i = 0; i < bytes; ++i) clockElf[i] = ((const uint8_t*)module.start)[i];
        bool patched = false;
        for (uint32_t i = 0; i < plan.segmentCount; ++i) {
            const Elf32LoadSegment& s = plan.segments[i];
            const uint32_t va = GTOS_CLOCK_PROBE_RECORD_VA;
            if (va < s.virtualAddress || va - s.virtualAddress > s.fileSize
                || s.fileSize - (va - s.virtualAddress) < sizeof(ClockProbeRecord)) continue;
            const uint32_t offset = s.fileOffset + va - s.virtualAddress;
            ClockRequire(Get32(clockElf + offset) == 1, "serialized initialized clock record");
            Put32(clockElf + offset + 20, CLOCK_SCENARIO); patched = true;
        }
        ClockRequire(patched, "clock record in initialized load data");
        uint32_t id = 0;
        ClockRequire(runtime.CreateElf(clockElf, bytes, id) && id, "actual clock ELF runtime admission");
        staticCost = ClockStaticCost(plan); return id;
    }
    void ClockPatterns(NativeRuntime& runtime, uint32_t id, const ClockProbeRecord& record) {
        ClockRequire(record.keep_base == 0x80000000U && record.keep_handle && record.keep_pages == 2,
            "actual fixed firstfit region with two retained pages");
        ClockRequire(runtime.ReadMemory(id, record.keep_base, clockPages, sizeof(clockPages)), "read both retained clock pages");
        for (uint32_t page = 0; page < 2; ++page)
            for (uint32_t byte = 0; byte < 4096; ++byte)
                ClockRequire(clockPages[page * 4096 + byte] == (uint8_t)(byte ^ (page ? 0xA9 : 0x37)),
                    "whole retained pages match independent byte pattern");
    }
    void ClockKernelSample(TaskManager& tasks) {
        GtosClockReadResult value; ClockA5(&value, sizeof(value));
        const uint32_t flags = ClockFlags();
        const uint64_t before = ClockObserved();
        const int result = tasks.ReadClock(value);
        const uint64_t after = ClockObserved();
        ClockRequire((flags & 0x200) && (ClockFlags() & 0x200), "live boot snapshot preserves IF1");
        if (result == 0) ClockRequire(ClockMetadata(value) && value.delivered_ticks >= before
            && value.delivered_ticks <= after, "live boot metadata and independent IRQ bracket");
        else ClockRequire(result == GTOS_CLOCK_ERR_OVERFLOW && ClockSentinel(&value, sizeof(value)),
            "live exhausted snapshot sentinel and sticky error");
        InterruptGuard logGuard; // Sampling and IF1 preservation were verified before guarding output.
        printf((char*)"CLOCK KERNEL status="); printfHex32((uint32_t)result); printf((char*)" n_before="); ClockHex64(before);
        printf((char*)" n_after="); ClockHex64(after);
        printf((char*)" if1=00000001 payload="); ClockPayload(value); printf((char*)"\n");
    }
    void ClockSurvivors(NativeRuntime& runtime, TaskManager& tasks, uint32_t cpuPeer, uint32_t beforeCpu,
                        uint32_t beforeYield, uint32_t beforeRing0, uint32_t beforeBoot, uint64_t beforeIrqs) {
        WaitTicks(tasks, 8);
        uint32_t data[4]; NativeStatus cpu = {}, peer = {};
        ClockRequire(runtime.ReadMemory(cpuPeer, NativeRuntime::DataAddress, data, sizeof(data))
            && runtime.Status(cpuPeer, cpu) && cpu.live && cpu.systemCalls == 0 && cpu.statistics.yields == 0
            && data[0] == 0x11223344 && data[3] != beforeCpu, "CPU-bound peer uses no syscall/yield and is preempted");
        ClockRequire(runtime.Status(clockPeerId, peer) && peer.live && peer.statistics.yields > beforeYield
            && ring0Progress != beforeRing0 && tasks.BootTicks() > beforeBoot && ClockObserved() > beforeIrqs,
            "real timer, yielding peer, ring0 and boot survive victim stop/reap");
        {
            InterruptGuard logGuard;
            printf((char*)"CLOCK SURVIVORS cpu_before="); printfHex32(beforeCpu); printf((char*)" cpu_after="); printfHex32(data[3]);
            printf((char*)" cpu_syscalls="); printfHex32(cpu.systemCalls); printf((char*)" cpu_yields="); printfHex32(cpu.statistics.yields);
            printf((char*)" peer_yields_before="); printfHex32(beforeYield); printf((char*)" peer_yields_after="); printfHex32(peer.statistics.yields);
            printf((char*)" ring0_before="); printfHex32(beforeRing0); printf((char*)" ring0_after="); printfHex32(ring0Progress);
            printf((char*)" boot_before="); printfHex32(beforeBoot); printf((char*)" boot_after="); printfHex32(tasks.BootTicks());
            printf((char*)" irq_before="); ClockHex64(beforeIrqs); printf((char*)" irq_after="); ClockHex64(ClockObserved()); printf((char*)"\n");
        }
        ClockKernelSample(tasks);
    }
}
asm(".section .text.native_clock_peer,\"ax\"\n.balign 16\n"
    ".global native_user_start,native_user_end\nnative_user_start:\n"
    "incl 0x4000200c\njmp native_user_start\nnative_user_end:\n.text\n");
extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    printf((char*)"NATIVE CLOCK SMOKE BOOT\nCLOCK SEED scenario="); printfHex32(CLOCK_SCENARIO);
    printf((char*)" n="); ClockHex64(CLOCK_SEED_TICKS); printf((char*)" us="); ClockHex64(CLOCK_SEED_US);
    printf((char*)" fraction="); printfHex32(CLOCK_SEED_FRACTION); printf((char*)"\n");
    NativeClockFixture::PrivateDispatch();
    GlobalDescriptorTable gdt;
    TaskManager tasks; NativeClockFixture::Seed(tasks); observedIrqs = CLOCK_SEED_TICKS;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    ClockRequire(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "initialize physical frames");
    observedFrames = &frames;
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end, (uint32_t)&kernel_readonly_start,
        (uint32_t)&kernel_readonly_end, (const MultibootInfo*)multiboot, 0, 0};
    ClockRequire(paging.prepareIdentity(frames, config), "prepare identity template");
    NativeRuntime runtime;
    ClockRequire(runtime.PrepareStacks(paging, frames) && paging.enable() && paging.sealForSharedProcessors(), "guarded frozen paging");
    ClockRequire(runtime.Activate(tasks, gdt, paging, frames), "activate real CPL3 runtime");
    ClockTimerTap timer(interrupts, tasks); ClockSyscallTap tap(interrupts, runtime, tasks, syscalls);
    kernelDirectory = paging.getStatistics().directoryAddress;
    const uint32_t initial = frames.getStatistics().freeFrames;
    const MultibootInfo* boot = (const MultibootInfo*)multiboot;
    ClockRequire((boot->flags & 8) && (boot->moduleCount == 4 || boot->moduleCount == 8), "exact raw4/optional genuine4 ELF modules");
    const MultibootModule* modules = (const MultibootModule*)boot->modules;
    Task ring0(&gdt, Ring0Task); ClockRequire(tasks.AddTask(&ring0), "ring0 peer");
    const uint32_t cpuPeer = Create(runtime, 0x11223344, 0);
    uint32_t peerStatic = 0; clockPeerId = ClockCreate(runtime, modules[2], peerStatic);
    asm volatile("outb %0,$0x43" : : "a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR & 255)));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR >> 8)));
    interrupts.Activate();
    const ClockProbeRecord peerRecord = ClockStage(runtime, tasks, clockPeerId);
    ClockRequire(peerRecord.mode == 2 && peerRecord.kind == 0, "yielding raw peer protocol"); ClockPatterns(runtime, clockPeerId, peerRecord);
    survivorFrames = frames.getStatistics().freeFrames;
    ClockRequire(initial - survivorFrames == 7 + peerStatic + 3, "independent peer static plus two-data/one-table accounting");
    {
        InterruptGuard logGuard;
        printf((char*)"CLOCK BASELINE kernel_cr3="); printfHex32(kernelDirectory); printf((char*)" initial="); printfHex32(initial);
        printf((char*)" survivor="); printfHex32(survivorFrames); printf((char*)"\n");
    }
    uint32_t executed = 0;
    for (uint32_t index = 0; index < boot->moduleCount; ++index) {
        const bool genuine = index >= 4; const uint32_t mode = index & 3;
        if (CLOCK_SCENARIO == 0 ? mode > 2 : (CLOCK_SCENARIO < 4 ? mode != 0
            : (CLOCK_SCENARIO == 4 ? (!genuine || mode != 3) : (genuine || mode != 3)))) continue;
        uint32_t data[4]; NativeStatus beforePeer = {};
        ClockRequire(runtime.ReadMemory(cpuPeer, NativeRuntime::DataAddress, data, sizeof(data))
            && runtime.Status(clockPeerId, beforePeer), "independent peer snapshots");
        const uint32_t beforeCpu = data[3], beforeRing0 = ring0Progress, beforeBoot = tasks.BootTicks();
        const uint64_t beforeIrqs = ClockObserved();
        const uint32_t callsBefore = clockCalls, errorsBefore = clockErrors;
        uint32_t staticCost = 0; const uint32_t id = ClockCreate(runtime, modules[index], staticCost);
        ClockProbeRecord record = ClockStage(runtime, tasks, id); NativeStatus status = {};
        ClockRequire(record.mode == mode && record.kind == (genuine ? 1U : 0U) && record.scenario == CLOCK_SCENARIO
            && record.first_result == 0 && ClockMetadata(record.first) && record.keep_handle != peerRecord.keep_handle,
            "actual record identity, first typed payload and distinct region ownership");
        if (mode == 2) { ClockPatterns(runtime, id, record); ClockRequire(runtime.RequestExit(id, 73), "RequestExit retained clock consumer"); }
        status = WaitStopped(runtime, tasks, id); record = ClockRecord(runtime, id); ClockRecordLog(record);
        ClockRequire(status.observedCs == 0x23 && status.observedCr3 == status.directory
            && status.directory != kernelDirectory && status.systemCalls, "actual CPL3/private CR3/syscall observations");
        ClockPatterns(runtime, id, record);
        ClockRequire(survivorFrames - frames.getStatistics().freeFrames == staticCost + 3,
            "queries have no allocations; only actual static plus explicitly retained VM pages");
        if (mode == 1) ClockRequire(status.faultVector == 14 && status.faultAddress == GTOS_CLOCK_PROBE_GUARD_VA
            && status.faultError == 6 && status.exitCode == (0x80000000U | 14), "exact user PF6 guard fault");
        else if (mode == 2) ClockRequire(status.exitCode == 73 && !status.faultVector, "external cancellation exit");
        else if (mode == 3 && genuine) ClockRequire(status.exitCode == GTOS_CLOCK_PROBE_FATAL && !status.faultVector
            && record.error == GTOS_CLOCK_PROBE_FATAL && record.clock_failures == 1 && record.last_result == 0
            && record.last.microseconds > 0x7FFFFFFFFFFFFFFDULL, "genuine TimeTicks fails signed range before cast/offset");
        else ClockRequire(status.exitCode == 0 && !status.faultVector && record.stage == 2, "positive clock consumer completed normally");
        if (mode == 0) ClockRequire(ClockMetadata(record.last) && record.last.microseconds > record.first.microseconds
            && record.last.delivered_ticks > record.first.delivered_ticks && record.elapsed_us,
            "actual coarse time and genuine/raw elapsed progress");
        if (mode == 0 && !genuine && CLOCK_SCENARIO >= 1 && CLOCK_SCENARIO <= 3) {
            const uint64_t boundary = CLOCK_SCENARIO == 1 ? 0x80000000ULL : 0x100000000ULL;
            ClockRequire((CLOCK_SCENARIO == 3 ? record.first.microseconds : record.first.delivered_ticks) < boundary
                && (CLOCK_SCENARIO == 3 ? record.last.microseconds : record.last.delivered_ticks) >= boundary,
                "first and last actual raw samples straddle selected rollover without reseeding live clock");
        }
        if (genuine && mode < 3) ClockRequire(record.elapsed_us >= 20000
            && record.elapsed_us == record.last.microseconds - record.first.microseconds,
            "genuine ElapsedTimer result equals independent full-width snapshot delta");
        if (mode == 3 && genuine) ClockRequire(record.first.microseconds <= 0x7FFFFFFFFFFFFFFDULL,
            "genuine signed-boundary first sample remains representable");
        if (mode == 3 && !genuine) ClockRequire(record.error == (uint32_t)GTOS_CLOCK_ERR_OVERFLOW
            && record.last_result == (uint32_t)GTOS_CLOCK_ERR_OVERFLOW && record.clock_failures == 1
            && record.sentinel_checks >= 16 && record.sentinel_bytes >= 16 * 48, "sticky typed uint64 overflow output sentinels");
        if (mode == 0 && !genuine) ClockRequire(record.sentinel_checks >= 20 && record.sentinel_bytes
            && record.sentinel_before == 0xA5A5A5A5U && record.sentinel_after == 0xA5A5A5A5U,
            "raw wire rejection and overlap guard sentinels");
        {
            InterruptGuard logGuard;
            printf(genuine ? (char*)"V8 CLOCK CASE mode=" : (char*)"RAW CLOCK CASE mode="); printfHex32(mode);
            printf((char*)" scenario="); printfHex32(CLOCK_SCENARIO); printf((char*)" cs="); printfHex32(status.observedCs);
            printf((char*)" cr3="); printfHex32(status.observedCr3); printf((char*)" cr2="); printfHex32(status.faultAddress);
            printf((char*)" pf="); printfHex32(status.faultError); printf((char*)" exit="); printfHex32(status.exitCode);
            printf((char*)" static="); printfHex32(staticCost); printf((char*)" dynamic=00000003 calls="); printfHex32(clockCalls - callsBefore);
            printf((char*)" errors="); printfHex32(clockErrors - errorsBefore); printf((char*)" retained=");
            printfHex32(survivorFrames - frames.getStatistics().freeFrames); printf((char*)"\n");
        }
        ClockRequire(runtime.Reap() == 1 && frames.getStatistics().freeFrames == survivorFrames, "exact deferred victim Reap baseline");
        {
            InterruptGuard logGuard;
            printf((char*)"CLOCK REAP free="); printfHex32(frames.getStatistics().freeFrames); printf((char*)" expected="); printfHex32(survivorFrames); printf((char*)"\n");
        }
        ClockPatterns(runtime, clockPeerId, peerRecord);
        ClockSurvivors(runtime, tasks, cpuPeer, beforeCpu, beforePeer.statistics.yields, beforeRing0, beforeBoot, beforeIrqs); ++executed;
    }
    ClockRequire(executed && (CLOCK_SCENARIO != 4 || boot->moduleCount == 8), "exact selected scenario has genuine/raw cases");
    ClockRequire(runtime.RequestExit(clockPeerId, 74) && runtime.RequestExit(cpuPeer, 0) && runtime.Reap() == 2
        && frames.getStatistics().freeFrames == initial, "final two-peer original allocator baseline");
    const uint64_t beforeFinal = ClockObserved(); WaitTicks(tasks, 8);
    ClockRequire(ClockObserved() > beforeFinal && ring0Progress && tasks.BootTicks(), "real PIT and boot continue after final Reap");
    ClockKernelSample(tasks);
    InterruptGuard finalLogGuard; // Count and low32 legacy diagnostics refer to one final snapshot.
    printf((char*)"CLOCK FINAL free="); printfHex32(frames.getStatistics().freeFrames); printf((char*)" expected="); printfHex32(initial);
    printf((char*)" n="); ClockHex64(ClockObserved()); printf((char*)" delivered="); printfHex32(deliveredIrqs);
    printf((char*)" calls="); printfHex32(clockCalls); printf((char*)" legacy="); printfHex32(legacyCalls);
    printf((char*)" errors="); printfHex32(clockErrors); printf((char*)" preserved="); printfHex32(preservedCalls);
    printf((char*)" cases="); printfHex32(executed); printf((char*)"\n"); ClockFinish(true);
}
