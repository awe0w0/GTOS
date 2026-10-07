// Reuse actual bootstrap helpers; GC discards the old acceptance entry.
#define NativeProcessSmoke NativeHeapUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include "../apps/native_heap_probe/record.h"

namespace {
    const uint32_t HeapRecordAddress = 0x40020000U;
    uint8_t heapElfCopy[65536], heapBytes[8192];
    void HeapFinish(bool pass) __attribute__((noreturn));
    void HeapFinish(bool pass) {
        printf(pass ? (char*)"NATIVE HEAP SMOKE PASS\n" : (char*)"NATIVE HEAP SMOKE FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void HeapRequire(bool value, const char* why) {
        if (!value) { printf((char*)"FAILED HEAP "); printf((char*)why); printf((char*)"\n"); HeapFinish(false); }
    }
    HeapProbeRecord HeapReadRecord(NativeRuntime& runtime, uint32_t id) {
        HeapProbeRecord record = {};
        HeapRequire(runtime.ReadMemory(id, HeapRecordAddress, &record, sizeof(record)), "read actual ELF heap record");
        return record;
    }
    HeapProbeRecord HeapWaitStage(NativeRuntime& runtime, TaskManager& tasks, uint32_t id) {
        const uint32_t begin = tasks.Ticks();
        for (;;) {
            const HeapProbeRecord record = HeapReadRecord(runtime, id);
            if (record.version == 1 && record.stage) return record;
            NativeStatus status = {};
            HeapRequire(runtime.Status(id, status) && status.live, "probe remains live until stage announcement");
            HeapRequire((uint32_t)(tasks.Ticks() - begin) < 800, "bounded real heap stage deadline");
            WaitTicks(tasks, 1);
        }
    }
    uint32_t HeapStaticFrameCost(const Elf32LoadPlan& plan) {
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
        const uint32_t stackIndex = NativeRuntime::UserStackBottom >> 22;
        if (!(seen[stackIndex >> 5] & (1U << (stackIndex & 31)))) ++tables;
        return plan.pageCount + 2 + tables + 1;
    }
    uint32_t HeapDynamicFrameCost(const HeapProbeRecord& record) {
        if (record.mode == 3) return 0;
        // Oracle counts the promised full RW arena independently of allocator
        // headers. This fixture admits no retained non-heap region; its exact
        // first-fit base is 0x80000000, disjoint from all ELF/stack PDEs.
        return 16 + ((record.base >> 22) == ((record.base + 65535U) >> 22) ? 1 : 2);
    }
    uint32_t HeapCreateElf(NativeRuntime& runtime, const MultibootModule& module, uint32_t& staticFrames) {
        const uint32_t bytes = module.end - module.start;
        HeapRequire(module.end >= module.start && bytes && bytes <= sizeof(heapElfCopy), "native ELF <=64KiB");
        const uint8_t* source = (const uint8_t*)module.start;
        Elf32LoadPlan plan;
        HeapRequire(ValidateElf32(source, bytes, plan) && plan.pageCount <= ProcessAddressSpace::MaximumPages - 2,
            "real ELF32 admission respects static plus stack budget");
        bool found = false;
        for (uint32_t i = 0; i < bytes; ++i) heapElfCopy[i] = source[i];
        for (uint32_t i = 0; i < plan.segmentCount; ++i) {
            const Elf32LoadSegment& segment = plan.segments[i];
            if (HeapRecordAddress < segment.virtualAddress
                || HeapRecordAddress - segment.virtualAddress > segment.fileSize
                || segment.fileSize - (HeapRecordAddress - segment.virtualAddress) < sizeof(HeapProbeRecord)) continue;
            HeapRequire(Get32(heapElfCopy + segment.fileOffset + HeapRecordAddress - segment.virtualAddress) == 1,
                "initialized actual probe record version");
            found = true;
        }
        HeapRequire(found, "probe record occupies initialized PT_LOAD bytes");
        uint32_t id = 0;
        HeapRequire(runtime.CreateElf(heapElfCopy, bytes, id) && id, "admit real C/C++ heap ELF through runtime");
        staticFrames = HeapStaticFrameCost(plan);
        return id;
    }
    void HeapPrivateContext(const NativeStatus& status, uint32_t kernelDirectory) {
        HeapRequire(status.observedCs == 0x23 && status.observedCr3 == status.directory
            && status.directory != kernelDirectory && status.systemCalls,
            "observed actual CPL3/private CR3 and production int80 calls");
    }
    void HeapRecordGeometry(const HeapProbeRecord& record) {
        HeapRequire(record.version == 1 && record.stage < 0x80000000U && record.checks
            && record.handle && record.handle <= 0x7FFFFFFFU && !(record.base & 4095U)
            && record.base == ProcessAddressSpace::DynamicBase, "live/released arena metadata and positive checks");
    }
    void HeapBlock(NativeRuntime& runtime, uint32_t id, uint32_t base, uint32_t pointer,
                   uint32_t bytes, uint32_t seed, bool zero) {
        HeapRequire(bytes == 4096 && pointer >= base && pointer <= base + 65536U - bytes
            && !(pointer & 15U), "public aligned allocation spans the owned arena");
        HeapRequire(runtime.ReadMemory(id, pointer, heapBytes, bytes), "kernel reads all actual allocation bytes");
        for (uint32_t i = 0; i < bytes; ++i)
            HeapRequire(heapBytes[i] == (zero ? 0U : (uint8_t)(i ^ seed)),
                "independent full allocation byte oracle");
    }
    void HeapPatterns(NativeRuntime& runtime, uint32_t id, const HeapProbeRecord& record, bool consumer) {
        HeapRecordGeometry(record);
        if (record.mode == 3) {
            HeapRequire(!record.primary && !record.primaryBytes && !record.neighbor && !record.neighborBytes,
                "last-free record retains no allocation pointers");
            HeapRequire(!runtime.ReadMemory(id, record.base, heapBytes, 1), "last free actually unmapped arena");
            return;
        }
        HeapBlock(runtime, id, record.base, record.primary, record.primaryBytes, 0x37, record.mode == 4);
        if (record.mode == 4 || (!consumer && record.mode == 6)) {
            HeapRequire(!record.neighbor && !record.neighborBytes, "one retained allocation mode");
        } else {
            HeapRequire(record.primary != record.neighbor
                && (record.primary + 4096U <= record.neighbor || record.neighbor + 4096U <= record.primary),
                "separate full allocation spans");
            HeapBlock(runtime, id, record.base, record.neighbor, record.neighborBytes, 0xA9, false);
        }
    }
    void HeapSurvivors(NativeRuntime& runtime, TaskManager& tasks, uint32_t cpuPeer, uint32_t heapPeer,
                       uint32_t beforeCpu, uint32_t beforeYield, uint32_t beforeRing0, uint32_t beforeBoot) {
        WaitTicks(tasks, 10);
        uint32_t data[4]; NativeStatus state = {}, cpu = {};
        HeapRequire(runtime.ReadMemory(cpuPeer, NativeRuntime::DataAddress, data, sizeof(data))
            && data[0] == 0x11223344 && data[3] != beforeCpu
            && runtime.Status(cpuPeer, cpu) && cpu.live && !cpu.systemCalls && !cpu.statistics.yields,
            "CPU-bound peer progresses with zero syscalls/yields through real timer preemption");
        HeapRequire(runtime.Status(heapPeer, state) && state.live && state.statistics.yields > beforeYield
            && ring0Progress != beforeRing0 && tasks.BootTicks() > beforeBoot,
            "heap peer, ring0 and boot all progress after victim stop/reap");
    }
}
// No syscall or yield: progress proves real timer preemption of native code.
asm(".section .text.native_heap_peer,\"ax\"\n.balign 16\n"
    ".global native_user_start,native_user_end\nnative_user_start:\n"
    "incl 0x4000200c\njmp native_user_start\nnative_user_end:\n.text\n");

extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    printf((char*)"NATIVE HEAP SMOKE BOOT\n");
    GlobalDescriptorTable gdt;
    HeapRequire(gdt.CodeSegmentSelector() == 0x10 && gdt.DataSegmentSelector() == 0x18, "kernel selectors");
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    HeapRequire(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frames initialize");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end, (const MultibootInfo*)multiboot, 0, 0};
    HeapRequire(paging.prepareIdentity(frames, config), "identity prepare");
    NativeRuntime runtime;
    HeapRequire(runtime.PrepareStacks(paging, frames), "retained guarded kernel stacks");
    HeapRequire(paging.enable() && paging.sealForSharedProcessors(), "frozen enabled kernel template");
    HeapRequire(runtime.Activate(tasks, gdt, paging, frames), "actual BSP native runtime");
    const uint32_t kernelDirectory = paging.getStatistics().directoryAddress;
    const uint32_t baseline = frames.getStatistics().freeFrames;
    Task ring0(&gdt, Ring0Task); HeapRequire(tasks.AddTask(&ring0), "ring0 peer");
    const MultibootInfo* boot = (const MultibootInfo*)multiboot;
    HeapRequire((boot->flags & 8) && boot->moduleCount >= 8, "eight independent raw C heap modules");
    const MultibootModule* modules = (const MultibootModule*)boot->modules;
    const uint32_t cpuPeer = Create(runtime, 0x11223344, 0);
    uint32_t peerStaticFrames = 0;
    const uint32_t heapPeer = HeapCreateElf(runtime, modules[2], peerStaticFrames);
    asm volatile("outb %0,$0x43" : : "a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(11932 & 255)));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(11932 >> 8)));
    interrupts.Activate();
    const HeapProbeRecord peerRecord = HeapWaitStage(runtime, tasks, heapPeer);
    HeapRequire(peerRecord.mode == 2 && peerRecord.stage == 1, "resident real heap peer");
    HeapPatterns(runtime, heapPeer, peerRecord, false);
    NativeStatus peerState = {}; HeapRequire(runtime.Status(heapPeer, peerState), "peer state");
    HeapPrivateContext(peerState, kernelDirectory);
    const uint32_t survivorBaseline = frames.getStatistics().freeFrames;
    HeapRequire(baseline - survivorBaseline == 7 + peerStaticFrames + HeapDynamicFrameCost(peerRecord),
        "peer physical cost is exact ELF/stack/tables plus full 64KiB heap");
    printf((char*)"HEAP SURVIVORS kernel_cr3="); printfHex32(kernelDirectory);
    printf((char*)" initial_free="); printfHex32(baseline);
    printf((char*)" survivor_free="); printfHex32(survivorBaseline); printf((char*)"\n");
    for (uint32_t moduleIndex = 0; moduleIndex < boot->moduleCount; ++moduleIndex) {
        uint32_t beforeData[4]; NativeStatus beforePeer = {};
        HeapRequire(runtime.ReadMemory(cpuPeer, NativeRuntime::DataAddress, beforeData, sizeof(beforeData))
            && runtime.Status(heapPeer, beforePeer), "snapshot survivors");
        const uint32_t beforeRing0 = ring0Progress, beforeBoot = tasks.BootTicks();
        uint32_t staticFrames = 0;
        const uint32_t id = HeapCreateElf(runtime, modules[moduleIndex], staticFrames);
        HeapProbeRecord record = HeapWaitStage(runtime, tasks, id);
        const bool consumer = moduleIndex >= 8;
        HeapRequire(record.mode <= (consumer ? 5U : 7U), "precise raw/SDK consumer mode set");
        NativeStatus status = {};
        if (record.mode == 2) {
            HeapPatterns(runtime, id, record, consumer);
            HeapRequire(runtime.RequestExit(id, 73), "RequestExit holds allocated C/C++ heap");
            HeapRequire(runtime.Status(id, status) && !status.live && status.exitCode == 73 && !status.faultVector,
                "external cancellation stops resident native heap process");
        } else {
            status = WaitStopped(runtime, tasks, id);
            record = HeapReadRecord(runtime, id);
            if (record.mode == 1 || (!consumer && record.mode == 7)) {
                const uint32_t address = record.mode == 1 ? NativeRuntime::UserStackBottom - 4U : 0x40000000U;
                const uint32_t bits = record.mode == 1 ? 6U : 7U;
                HeapRequire(record.stage == 1 && status.exitCode == (0x80000000U | 14) && status.faultVector == 14
                    && status.faultAddress == address && (status.faultError & 7) == bits,
                    "real guard/RO caller-output access yields exact CPL3 PF");
            } else if (record.mode == 5 || (!consumer && record.mode == 6)) {
                const uint32_t code = consumer ? 0x48000010U : 0x48000002U;
                HeapRequire(record.stage == 1 && status.exitCode == code && !status.faultVector,
                    "real ordinary-new OOM or designed bad-free failure exits with retained heap");
            } else {
                HeapRequire(record.stage == 2 && status.exitCode == 0 && !status.faultVector,
                    "positive/release/fresh-zero heap probe completes normally");
            }
        }
        HeapPatterns(runtime, id, record, consumer);
        HeapRequire(record.base == peerRecord.base && record.handle != peerRecord.handle,
            "same user VA with globally distinct process-owned heap handles");
        HeapPrivateContext(status, kernelDirectory);
        const uint32_t retained = survivorBaseline - frames.getStatistics().freeFrames;
        HeapRequire(retained == staticFrames + HeapDynamicFrameCost(record),
            "stopped heap data/tables stay retained until explicit Reap; released heap owns zero frames");
        printf(consumer ? (char*)"V8 HEAP CASE mode=" : (char*)"RAW HEAP CASE mode="); printfHex32(record.mode);
        printf((char*)" stage="); printfHex32(record.stage); printf((char*)" checks="); printfHex32(record.checks);
        printf((char*)" cs="); printfHex32(status.observedCs); printf((char*)" cr3="); printfHex32(status.observedCr3);
        printf((char*)" cr2="); printfHex32(status.faultAddress); printf((char*)" error="); printfHex32(status.faultError);
        printf((char*)" exit="); printfHex32(status.exitCode); printf((char*)" base="); printfHex32(record.base);
        printf((char*)" handle="); printfHex32(record.handle); printf((char*)" retained="); printfHex32(retained);
        printf((char*)" static="); printfHex32(staticFrames); printf((char*)" dynamic="); printfHex32(HeapDynamicFrameCost(record));
        printf((char*)"\n");
        HeapRequire(runtime.Reap() == 1 && frames.getStatistics().freeFrames == survivorBaseline,
            "normal Exit/fault/RequestExit restores exact survivor physical baseline");
        printf((char*)"HEAP REAP free="); printfHex32(frames.getStatistics().freeFrames);
        printf((char*)" expected="); printfHex32(survivorBaseline); printf((char*)"\n");
        HeapPatterns(runtime, heapPeer, peerRecord, false);
        HeapSurvivors(runtime, tasks, cpuPeer, heapPeer, beforeData[3], beforePeer.statistics.yields, beforeRing0, beforeBoot);
    }
    HeapRequire(runtime.RequestExit(heapPeer, 74) && runtime.RequestExit(cpuPeer, 0), "stop last heap and CPU peers");
    HeapRequire(runtime.Reap() == 2 && frames.getStatistics().freeFrames == baseline, "original exact physical baseline");
    printf((char*)"HEAP FINAL free="); printfHex32(frames.getStatistics().freeFrames);
    printf((char*)" expected="); printfHex32(baseline); printf((char*)"\n");
    WaitTicks(tasks, 10); HeapRequire(ring0Progress && tasks.BootTicks(), "ring0/boot progress after final heap Reap");
    HeapFinish(true);
}
