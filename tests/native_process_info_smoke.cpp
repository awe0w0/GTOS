// The accepted baseline supplies boot helpers; its old entry is discarded by GC.
#define NativeProcessSmoke NativeProcessInfoUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include <process/info_abi.h>
#include <memory/criticalsection.h>
#include "../apps/native_process_info_probe/record.h"

namespace {
    PhysicalMemoryManager* infoFrames;
    uint32_t infoIds[16], infoIdCount, infoKernelDirectory;
    volatile uint32_t infoCalls[16], infoErrors[16], infoPreserved;
    uint8_t infoPages[8192];
    void InfoFinish(bool pass) __attribute__((noreturn));
    void InfoFinish(bool pass) {
        printf(pass ? (char*)"NATIVE PROCESS INFO SMOKE PASS\n" : (char*)"NATIVE PROCESS INFO SMOKE FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void InfoRequire(bool value, const char* why) {
        if (!value) { asm volatile("cli" : : : "memory"); printf((char*)"FAILED PROCESS INFO "); printf((char*)why); printf((char*)"\n"); InfoFinish(false); }
    }
    uint32_t InfoCr3() { uint32_t value; asm volatile("mov %%cr3,%0" : "=r"(value)); return value; }
    uint32_t InfoFlags() { uint32_t value; asm volatile("pushfl; popl %0" : "=r"(value)); return value; }
    void InfoRemember(uint32_t id) {
        InfoRequire(id && id < 16 && infoIdCount < 16, "bounded independent admission IDs");
        for (uint32_t i = 0; i < infoIdCount; ++i) InfoRequire(infoIds[i] != id, "no process ID reuse after Reap");
        infoIds[infoIdCount++] = id;
    }
    bool InfoMetadata(const GtosProcessInfoResult& value, uint32_t id) {
        return value.version == 1 && value.process_id == id && value.thread_id == id
            && value.stack_begin == 0xBFFFD000U && value.stack_end == 0xBFFFF000U
            && value.user_begin == 0x40000000U && value.user_end == 0xC0000000U
            && value.page_bytes == 4096 && value.maximum_pages == 256 && value.maximum_regions == 32
            && value.maximum_parallel_threads == 1 && value.scheduler_cpu_count == 1;
    }
    class InfoSyscallTap : public InterruptHandler {
        NativeRuntime& runtime;
        TaskManager& tasks;
        SyscallHandler& original;
    public:
        InfoSyscallTap(InterruptsManager& interrupts, NativeRuntime& current, TaskManager& scheduler, SyscallHandler& handler)
            : InterruptHandler(&interrupts, 0x80), runtime(current), tasks(scheduler), original(handler) {}
        uint32_t HandlerInterrupt(uint32_t esp) {
            CPUState* cpu = (CPUState*)esp;
            if (cpu->eax != GTOS_SYS_PROCESS_INFO) return original.HandlerInterrupt(esp);
            InfoRequire(!(InfoFlags() & 0x200) && cpu->cs == 0x23, "real CPL3 query enters with IF clear");
            const CPUState registers = *cpu;
            Task* current = tasks.CurrentTask();
            InfoRequire(current && current->UserMode(), "actual user task selected");
            const TaskStatistics statistics = current->Statistics();
            const uint32_t directory = InfoCr3(), before = infoFrames->getStatistics().freeFrames;
            const uint32_t ticks = tasks.Ticks(), count = tasks.TaskCount();
            uint32_t id = 0; NativeStatus status = {};
            for (uint32_t i = 0; i < infoIdCount; ++i) {
                NativeStatus candidate = {};
                // A reused slot retains only its latest status. Admission history
                // still proves ID uniqueness; only currently present live slots
                // participate in mapping the hardware CR3 to its caller.
                if (!runtime.Status(infoIds[i], candidate)) continue;
                if (candidate.live && candidate.directory == directory) { id = candidate.id; status = candidate; }
            }
            InfoRequire(id && directory != infoKernelDirectory, "actual private CR3 matches admitted caller");
            const uint32_t returned = original.HandlerInterrupt(esp);
            const TaskStatistics after = current->Statistics();
            InfoRequire(returned == esp && tasks.CurrentTask() == current && InfoCr3() == directory
                && tasks.Ticks() == ticks && tasks.TaskCount() == (int)count
                && statistics.runTicks == after.runTicks && statistics.dispatches == after.dispatches
                && statistics.yields == after.yields && statistics.sleeps == after.sleeps
                && infoFrames->getStatistics().freeFrames == before,
                "query has no allocation, scheduling or clock side effect");
            NativeStatus observed = {};
            InfoRequire(runtime.Status(id, observed) && observed.live && observed.id == id
                && observed.systemCalls == status.systemCalls + 1 && observed.observedCr3 == directory
                && observed.observedCs == 0x23, "production runtime records the same real caller");
            InfoRequire(cpu->ebx == registers.ebx && cpu->ecx == registers.ecx && cpu->edx == registers.edx
                && cpu->esi == registers.esi && cpu->edi == registers.edi && cpu->ebp == registers.ebp
                && cpu->ds == registers.ds && cpu->es == registers.es && cpu->fs == registers.fs && cpu->gs == registers.gs
                && cpu->eip == registers.eip && cpu->cs == registers.cs && cpu->esp == registers.esp && cpu->ss == registers.ss
                && (cpu->eflags & 0x3302U) == 0x202U, "non-result registers, segments and user continuation preserved");
            ++infoCalls[id]; ++infoPreserved;
            if (cpu->eax) ++infoErrors[id];
            printf((char*)"INFO CALL id="); printfHex32(id); printf((char*)" status="); printfHex32(cpu->eax);
            printf((char*)" request="); printfHex32(registers.ebx); printf((char*)" bytes="); printfHex32(registers.ecx);
            printf((char*)" free_before="); printfHex32(before); printf((char*)" free_after=");
            printfHex32(infoFrames->getStatistics().freeFrames); printf((char*)" preserved=00000001\n");
            return returned;
        }
    };
    ProcessInfoProbeRecord InfoRecord(NativeRuntime& runtime, uint32_t id) {
        ProcessInfoProbeRecord record = {};
        InfoRequire(runtime.ReadMemory(id, GTOS_PROCESS_INFO_RECORD_VA, &record, sizeof(record)), "read actual 160B guest record");
        return record;
    }
    ProcessInfoProbeRecord InfoStage(NativeRuntime& runtime, TaskManager& tasks, uint32_t id) {
        const uint32_t start = tasks.Ticks();
        for (;;) {
            const ProcessInfoProbeRecord record = InfoRecord(runtime, id);
            if (record.version == 1 && record.stage) {
                NativeStatus status = {};
                InfoRequire(runtime.Status(id, status), "ready caller status present");
                InterruptGuard guard;
                printf((char*)"INFO READY id="); printfHex32(id); printf((char*)" elapsed=");
                printfHex32(tasks.Ticks() - start); printf((char*)" queries="); printfHex32(record.raw_query_count);
                printf((char*)" sentinels="); printfHex32(record.sentinel_checks); printf((char*)" run_ticks=");
                printfHex32(status.statistics.runTicks); printf((char*)" dispatches=");
                printfHex32(status.statistics.dispatches); printf((char*)"\n");
                return record;
            }
            NativeStatus status = {};
            InfoRequire(runtime.Status(id, status) && status.live, "probe reaches complete stage without early exit");
            if ((uint32_t)(tasks.Ticks() - start) >= 2000) {
                InterruptGuard guard;
                printf((char*)"INFO TIMEOUT id="); printfHex32(id); printf((char*)" elapsed=");
                printfHex32(tasks.Ticks() - start); printf((char*)" queries="); printfHex32(record.raw_query_count);
                printf((char*)" sentinels="); printfHex32(record.sentinel_checks); printf((char*)" run_ticks=");
                printfHex32(status.statistics.runTicks); printf((char*)" dispatches=");
                printfHex32(status.statistics.dispatches); printf((char*)"\n");
                InfoRequire(false, "bounded full-byte guest initialization");
            }
            WaitTicks(tasks, 1);
        }
    }
    uint32_t InfoStaticCost(const Elf32LoadPlan& plan) {
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
    uint32_t InfoCreate(NativeRuntime& runtime, const MultibootModule& module, uint32_t& cost) {
        InterruptGuard guard; // Publish the independent ID before its first possible dispatch.
        InfoRequire(module.end > module.start && module.end - module.start <= 65536, "bounded real ELF input");
        Elf32LoadPlan plan;
        InfoRequire(ValidateElf32((const uint8_t*)module.start, module.end - module.start, plan)
            && plan.pageCount <= ProcessAddressSpace::MaximumPages - 2, "real ELF32 admission geometry");
        uint32_t id = 0;
        InfoRequire(runtime.CreateElf((const uint8_t*)module.start, module.end - module.start, id), "actual ELF admission");
        InfoRemember(id); cost = InfoStaticCost(plan); return id;
    }
    void InfoPatterns(NativeRuntime& runtime, uint32_t id, const ProcessInfoProbeRecord& record) {
        InfoRequire(record.keep_base == 0x80000000U && record.keep_handle && record.keep_pages == 2,
            "guest retains only its own two committed pages");
        InfoRequire(runtime.ReadMemory(id, record.keep_base, infoPages, sizeof(infoPages)), "read whole retained user pages");
        for (uint32_t i = 0; i < sizeof(infoPages); ++i) InfoRequire(infoPages[i] == 0xA5, "all retained contents survive queries/stops");
    }
    void InfoLog(const ProcessInfoProbeRecord& record) {
        InterruptGuard guard;
        printf((char*)"INFO RECORD RAW words=");
        const uint8_t* bytes = (const uint8_t*)&record;
        for (uint32_t i = 0; i < sizeof(record); i += 4) { printf((char*)" "); printfHex32(Get32(bytes + i)); }
        printf((char*)"\n");
    }
}
asm(".section .text.native_info_peer,\"ax\"\n.balign 16\n"
    ".global native_user_start,native_user_end\nnative_user_start:\n"
    "incl 0x4000200c\njmp native_user_start\nnative_user_end:\n.text\n");
extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    printf((char*)"NATIVE PROCESS INFO SMOKE BOOT\n");
    GlobalDescriptorTable gdt; TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    InfoRequire(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "initialize real frames");
    infoFrames = &frames;
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end, (uint32_t)&kernel_readonly_start,
        (uint32_t)&kernel_readonly_end, (const MultibootInfo*)multiboot, 0, 0};
    InfoRequire(paging.prepareIdentity(frames, config), "prepare identity template");
    NativeRuntime runtime;
    InfoRequire(runtime.PrepareStacks(paging, frames) && paging.enable() && paging.sealForSharedProcessors(),
        "guarded sealed paging");
    InfoRequire(runtime.Activate(tasks, gdt, paging, frames), "activate production CPL3 runtime");
    infoKernelDirectory = paging.getStatistics().directoryAddress;
    InfoSyscallTap tap(interrupts, runtime, tasks, syscalls);
    const uint32_t initial = frames.getStatistics().freeFrames;
    const MultibootInfo* boot = (const MultibootInfo*)multiboot;
    InfoRequire((boot->flags & 8) && boot->moduleCount == 4, "exact four real probe modes");
    const MultibootModule* modules = (const MultibootModule*)boot->modules;
    Task ring0(&gdt, Ring0Task); InfoRequire(tasks.AddTask(&ring0), "independent ring0 peer");
    const uint32_t cpuPeer = Create(runtime, 0x11223344, 0); InfoRemember(cpuPeer);
    uint32_t peerCost = 0; const uint32_t infoPeer = InfoCreate(runtime, modules[3], peerCost);
    asm volatile("outb %0,$0x43" : : "a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR & 255)));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR >> 8)));
    interrupts.Activate();
    ProcessInfoProbeRecord peerRecord = InfoStage(runtime, tasks, infoPeer);
    InfoRequire(peerRecord.mode == 3 && InfoMetadata(peerRecord.first, infoPeer), "peer reports its actual admitted identity");
    InfoPatterns(runtime, infoPeer, peerRecord);
    const uint32_t survivors = frames.getStatistics().freeFrames;
    InfoRequire(initial - survivors == 7 + peerCost + 3, "independent two-peer physical accounting");
    {
    InterruptGuard guard;
    printf((char*)"INFO BASELINE initial="); printfHex32(initial); printf((char*)" survivor="); printfHex32(survivors);
    printf((char*)" cpu_peer="); printfHex32(cpuPeer); printf((char*)" info_peer="); printfHex32(infoPeer);
    printf((char*)" kernel_cr3="); printfHex32(infoKernelDirectory); printf((char*)"\n");
    }
    for (uint32_t round = 0; round < 2; ++round) for (uint32_t mode = 0; mode < 3; ++mode) {
        uint32_t data[4]; NativeStatus beforePeer = {};
        InfoRequire(runtime.ReadMemory(cpuPeer, NativeRuntime::DataAddress, data, sizeof(data))
            && runtime.Status(infoPeer, beforePeer) && beforePeer.live, "independent survivor snapshots");
        const uint32_t cpuBefore = data[3], ring0Before = ring0Progress, bootBefore = tasks.BootTicks();
        uint32_t cost = 0; const uint32_t id = InfoCreate(runtime, modules[mode], cost);
        ProcessInfoProbeRecord record = InfoStage(runtime, tasks, id);
        InfoRequire(record.mode == mode && InfoMetadata(record.first, id) && id != infoPeer
            && record.keep_handle != peerRecord.keep_handle, "same-VA private callers have distinct real identity and ownership");
        if (mode == 2) InfoRequire(runtime.RequestExit(id, 73), "external close request after completed queries");
        const NativeStatus status = WaitStopped(runtime, tasks, id);
        record = InfoRecord(runtime, id); InfoLog(record); InfoPatterns(runtime, id, record);
        InfoRequire(InfoMetadata(record.first, id) && InfoMetadata(record.last, id) && !record.error
            && record.failed_queries == infoErrors[id] && record.raw_query_count == infoCalls[id]
            && record.successful_queries + record.failed_queries == record.raw_query_count
            && record.sentinel_checks == 50 && record.sentinel_bytes == record.sentinel_checks * 8192,
            "actual payloads, rejected queries and full sentinels agree with independent trap observations");
        InfoRequire(status.observedCs == 0x23 && status.observedCr3 == status.directory
            && status.directory != infoKernelDirectory && !status.reaped && !status.live
            && survivors - frames.getStatistics().freeFrames == cost + 3, "stopped victim retains exact static/dynamic frames");
        if (mode == 1) InfoRequire(status.faultVector == 14 && status.faultError == 6
            && status.faultAddress == 0xBFFFCFFCU && status.exitCode == 0x8000000EU, "exact contained user guard fault");
        else if (mode == 2) InfoRequire(!status.faultVector && status.exitCode == 73, "external cancellation stopped only the victim");
        else InfoRequire(!status.faultVector && !status.exitCode && record.stage == 2, "normal native exit");
        {
            InterruptGuard guard;
            printf((char*)"INFO CASE round="); printfHex32(round); printf((char*)" mode="); printfHex32(mode);
            printf((char*)" id="); printfHex32(id); printf((char*)" calls="); printfHex32(infoCalls[id]);
            printf((char*)" errors="); printfHex32(infoErrors[id]); printf((char*)" cs="); printfHex32(status.observedCs);
            printf((char*)" cr3="); printfHex32(status.observedCr3); printf((char*)" exit="); printfHex32(status.exitCode);
            printf((char*)" pf="); printfHex32(status.faultError); printf((char*)" cr2="); printfHex32(status.faultAddress);
            printf((char*)" static="); printfHex32(cost); printf((char*)" dynamic=00000003\n");
        }
        InfoRequire(runtime.Reap() == 1 && frames.getStatistics().freeFrames == survivors, "exact victim deferred Reap");
        {
            InterruptGuard guard;
            printf((char*)"INFO REAP free="); printfHex32(frames.getStatistics().freeFrames);
            printf((char*)" expected="); printfHex32(survivors); printf((char*)"\n");
        }
        WaitTicks(tasks, 8); NativeStatus cpu = {}, peer = {};
        InfoRequire(runtime.ReadMemory(cpuPeer, NativeRuntime::DataAddress, data, sizeof(data))
            && runtime.Status(cpuPeer, cpu) && cpu.live && !cpu.systemCalls && !cpu.statistics.yields
            && data[0] == 0x11223344 && data[3] != cpuBefore, "CPU-bound peer remains genuinely preemptible without syscall");
        InfoRequire(runtime.Status(infoPeer, peer) && peer.live && peer.statistics.yields > beforePeer.statistics.yields
            && ring0Progress != ring0Before && tasks.BootTicks() > bootBefore, "querying peer, ring0 and boot survive victim Reap");
        peerRecord = InfoRecord(runtime, infoPeer);
        InfoRequire(InfoMetadata(peerRecord.first, infoPeer) && InfoMetadata(peerRecord.last, infoPeer), "peer identity remains stable");
        InfoPatterns(runtime, infoPeer, peerRecord);
        {
        InterruptGuard guard;
        printf((char*)"INFO SURVIVORS round="); printfHex32(round); printf((char*)" mode="); printfHex32(mode);
        printf((char*)" cpu_syscalls="); printfHex32(cpu.systemCalls); printf((char*)" cpu_yields=");
        printfHex32(cpu.statistics.yields); printf((char*)" progress=00000001\n");
        }
    }
    InfoRequire(runtime.RequestExit(infoPeer, 74) && runtime.RequestExit(cpuPeer, 0) && runtime.Reap() == 2
        && frames.getStatistics().freeFrames == initial, "original allocator baseline after both peers close");
    WaitTicks(tasks, 8);
    InterruptGuard finalGuard;
    printf((char*)"INFO FINAL free="); printfHex32(frames.getStatistics().freeFrames);
    printf((char*)" expected="); printfHex32(initial); printf((char*)" preserved="); printfHex32(infoPreserved);
    printf((char*)" admitted="); printfHex32(infoIdCount); printf((char*)" cases=00000006\n");
    InfoFinish(true);
}
