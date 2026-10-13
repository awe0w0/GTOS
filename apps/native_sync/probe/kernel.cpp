#include "fixture-helpers.h"
#include "record.h"

namespace {
    uint8_t syncElf[1048576];
    const uint32_t RecordAddress = 0x40100000U;
    uint32_t injectedClockErrors;
    void Finish(bool pass) {
        printf(pass ? (char*)"NATIVE SYNC PASS\n" : (char*)"NATIVE SYNC FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void Require(bool value, const char* why) {
        if (!value) { printf((char*)"FAILED SYNC "); printf((char*)why); printf((char*)"\n"); Finish(false); }
    }
    NativeSyncRecord Read(NativeRuntime& runtime, uint32_t id) {
        NativeSyncRecord record = {};
        Require(runtime.ReadMemory(id, RecordAddress, &record, sizeof(record)), "full CPL3 record");
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
        Require(!(seen[0x80000000U >> 27] & (1U << ((0x80000000U >> 22) & 31))), "dynamic table absent in ELF");
        return plan.pageCount + 2 + tables + 1;
    }
    uint32_t Spawn(NativeRuntime& runtime, const MultibootModule& module, uint32_t mode, uint32_t& cost) {
        Require(module.end > module.start && module.end - module.start <= sizeof(syncElf), "bounded ELF input");
        const uint32_t bytes = module.end - module.start;
        Elf32LoadPlan plan;
        Require(ValidateElf32((const uint8_t*)module.start, bytes, plan), "real ELF validation");
        for (uint32_t i = 0; i < bytes; ++i) syncElf[i] = ((const uint8_t*)module.start)[i];
        bool found = false;
        for (uint32_t i = 0; i < plan.segmentCount; ++i) {
            const Elf32LoadSegment& segment = plan.segments[i];
            if (RecordAddress < segment.virtualAddress || RecordAddress - segment.virtualAddress > segment.fileSize
                || segment.fileSize - (RecordAddress - segment.virtualAddress) < sizeof(NativeSyncRecord)) continue;
            uint8_t* record = syncElf + segment.fileOffset + RecordAddress - segment.virtualAddress;
            Require(Get32(record) == 1 && Get32(record + 4) == 0, "fresh initial state");
            Put32(record + 4, mode); found = true;
        }
        Require(found, "file backed record");
        uint32_t id = 0;
        Require(runtime.CreateElf(syncElf, bytes, id) && id, "production CPL3 admission");
        cost = Cost(plan); return id;
    }
    NativeStatus Wait(NativeRuntime& runtime, TaskManager& tasks, uint32_t id, uint32_t mode) {
        const uint32_t begin = tasks.Ticks();
        uint32_t waitingSince = 0;
        bool observedWait = false;
        for (;;) {
            NativeStatus status = {};
            Require(runtime.Status(id, status), "status retained");
            if (!status.live) return status;
            const NativeSyncRecord record = Read(runtime, id);
            if ((mode == 2 || mode == 3) && record.pending == mode) {
                uint32_t mutex[2] = {}, condition[2] = {};
                Require(runtime.ReadMemory(id, record.mutexAddress, mutex, sizeof(mutex))
                    && runtime.ReadMemory(id, record.conditionAddress, condition, sizeof(condition)), "waiting state readable");
                if (condition[0] == 1 && mutex[0] == 0 && mutex[1] == 0) {
                    if (!observedWait) { observedWait = true; waitingSince = tasks.Ticks(); }
                    if ((uint32_t)(tasks.Ticks() - waitingSince) >= 3) {
                        Require(runtime.RequestExit(id, 77), "cancel actual pending condition wait");
                        Require(runtime.Status(id, status) && !status.live, "cancel stopped");
                        return status;
                    }
                }
            }
            Require((uint32_t)(tasks.Ticks() - begin) < 2000, "bounded guest deadline");
            WaitTicks(tasks, 1);
        }
    }
}
namespace gtos {
    struct NativeClockFixture {
        static bool CopyCurrent(NativeRuntime& runtime, void* out, uint32_t address, uint32_t bytes) {
            NativeRuntime::Slot* slot = runtime.Current();
            return slot && slot->space.CopyFromUser(out, address, bytes);
        }
        static bool Exhausted(TaskManager& tasks, bool value) {
            const bool previous = tasks.clock.exhausted;
            tasks.clock.exhausted = value;
            return previous;
        }
    };
}
namespace {
    class ClockTap : public InterruptHandler {
        NativeRuntime& runtime; TaskManager& tasks; SyscallHandler& original;
    public:
        ClockTap(InterruptsManager& interrupts, NativeRuntime& r, TaskManager& t, SyscallHandler& o)
            : InterruptHandler(&interrupts, 0x80), runtime(r), tasks(t), original(o) {}
        uint32_t HandlerInterrupt(uint32_t esp) {
            CPUState* cpu = (CPUState*)esp;
            if (cpu->eax != GTOS_SYS_REALTIME_READ) return original.HandlerInterrupt(esp);
            NativeSyncRecord record = {};
            Require(NativeClockFixture::CopyCurrent(runtime, &record, RecordAddress, sizeof(record)), "clock caller record");
            if (record.pending != 4) return original.HandlerInterrupt(esp);
            Require(record.mode == 4 && record.stage == 2 && !injectedClockErrors && cpu->cs == 0x23, "single CPL3 clock error");
            uint32_t mutex[2] = {}, condition[2] = {};
            Require(NativeClockFixture::CopyCurrent(runtime, mutex, record.mutexAddress, sizeof(mutex))
                && NativeClockFixture::CopyCurrent(runtime, condition, record.conditionAddress, sizeof(condition))
                && condition[0] == 1 && !mutex[0] && !mutex[1], "mutex released during clock read");
            // Test-only failure while IRQs are masked, restored before returning.
            const bool old = NativeClockFixture::Exhausted(tasks, true);
            const uint32_t returned = original.HandlerInterrupt(esp);
            NativeClockFixture::Exhausted(tasks, old);
            Require(!old && returned == esp && (int32_t)cpu->eax == GTOS_CLOCK_ERR_OVERFLOW, "real exhausted clock response");
            GtosClockReadResult restored = {};
            Require(tasks.ReadClock(restored) == 0, "clock producer restored");
            ++injectedClockErrors;
            return returned;
        }
    };
}
asm(".section .text.native_sync_integer_peer,\"ax\"\n.balign 16\n"
    ".global native_user_start,native_user_end\nnative_user_start:\n"
    "incl 0x4000200c\njmp native_user_start\nnative_user_end:\n.text\n");
extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    printf((char*)"NATIVE SYNC BOOT\n");
    GlobalDescriptorTable gdt;
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    Require(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frame bootstrap");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end, (const MultibootInfo*)multiboot, 0, 0};
    Require(paging.prepareIdentity(frames, config), "identity template");
    NativeRuntime runtime;
    Require(runtime.PrepareStacks(paging, frames), "guarded kernel stacks");
    Require(paging.enable() && paging.sealForSharedProcessors(), "sealed template");
    Require(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2), "native runtime with user SSE2");
    ClockTap tap(interrupts, runtime, tasks, syscalls);
    const uint32_t kernelDirectory = paging.getStatistics().directoryAddress;
    const uint32_t baseline = frames.getStatistics().freeFrames;
    Task ring0(&gdt, Ring0Task);
    Require(tasks.AddTask(&ring0), "ring0 survivor");
    const MultibootInfo* boot = (const MultibootInfo*)multiboot;
    Require((boot->flags & 8) && boot->moduleCount == 1, "one actual native ELF");
    const MultibootModule* modules = (const MultibootModule*)boot->modules;
    const uint32_t peer = Create(runtime, 0x11223344U, 0);
    const uint32_t survivorBaseline = frames.getStatistics().freeFrames;
    Require(baseline - survivorBaseline == 7, "peer frame cost");
    asm volatile("outb %0,$0x43" : : "a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(11931 & 255)));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(11931 >> 8)));
    interrupts.Activate();
    const uint32_t modes[] = {0,1,2,3,4,0};
    for (uint32_t n = 0; n < sizeof(modes) / 4; ++n) {
        uint32_t beforePeer[4], afterPeer[4];
        Require(runtime.ReadMemory(peer, NativeRuntime::DataAddress, beforePeer, sizeof(beforePeer)), "peer before");
        const uint32_t beforeRing0 = ring0Progress, beforeBoot = tasks.BootTicks();
        const uint32_t beforeWritten = runtime.Statistics().writtenBytes;
        uint32_t cost = 0;
        const uint32_t id = Spawn(runtime, modules[0], modes[n], cost);
        const NativeStatus status = Wait(runtime, tasks, id, modes[n]);
        const NativeSyncRecord record = Read(runtime, id);
        printf((char*)"SYNC OBSERVE error="); printfHex32(record.error);
        printf((char*)" checks="); printfHex32(record.checks);
        printf((char*)" exit="); printfHex32(status.exitCode); printf((char*)"\n");
        Require(status.observedCs == 0x23 && status.observedCr3 == status.directory
            && status.directory != kernelDirectory && !status.faultVector, "observed private CPL3 without fault");
        Require(record.version == 1 && record.mode == modes[n] && !record.error
            && record.rawChecks == 28 && record.stdChecks == 15 && record.waiterChecks == 6
            && record.region && record.base == 0x80000000U && record.length == 4096
            && record.mutexAddress && record.conditionAddress && !record.reserved, "complete boundary and waiter groups");
        uint32_t marker = 0, mutex[2] = {}, condition[2] = {};
        Require(runtime.ReadMemory(id, record.base, &marker, sizeof(marker)) && marker == 0xC011CA11U
            && runtime.ReadMemory(id, record.mutexAddress, mutex, sizeof(mutex))
            && runtime.ReadMemory(id, record.conditionAddress, condition, sizeof(condition)), "retained user resources");
        const uint32_t mode = modes[n];
        const bool cancelled = mode == 2 || mode == 3;
        Require(!mutex[0] && !mutex[1] && condition[0] == (cancelled ? 1U : 0U), "lock and waiter terminal state");
        Require(record.checks == (mode == 4 ? 57U : cancelled ? 54U : 53U), "exact checks completed");
        if (cancelled) Require(record.stage == 2 && record.pending == mode && status.exitCode == 77, "pending wait cancelled");
        else if (mode == 1) Require(record.stage == 2 && !record.pending && status.exitCode == 0x48000021U, "unowned standard wait aborts");
        else Require(record.stage == 3 && !record.pending && status.exitCode == 0, "normal return after reacquisition");
        Require(record.clockErrors == (mode == 4 ? 1U : 0U), "clock error propagation marker");
        const uint32_t written = runtime.Statistics().writtenBytes - beforeWritten;
        Require(mode == 1 ? written > 23 && written < 1024 : written == 0, "bounded error diagnostic");
        const uint32_t retainedCost = survivorBaseline - frames.getStatistics().freeFrames;
        Require(retainedCost >= cost + 2 && retainedCost < cost + 128, "bounded retained footprint");
        printf((char*)"SYNC CASE id="); printfHex32(id); printf((char*)" mode="); printfHex32(mode);
        printf((char*)" cost="); printfHex32(retainedCost); printf((char*)" words= ");
        const uint32_t* words = (const uint32_t*)&record;
        for (uint32_t i = 0; i < 16; ++i) { printfHex32(words[i]); printf((char*)" "); }
        printf((char*)"\n");
        Require(runtime.Reap() == 1 && frames.getStatistics().freeFrames == survivorBaseline, "exact victim reap");
        WaitTicks(tasks, 6);
        NativeStatus peerStatus = {};
        Require(runtime.Status(peer, peerStatus) && peerStatus.live && !peerStatus.systemCalls
            && runtime.ReadMemory(peer, NativeRuntime::DataAddress, afterPeer, sizeof(afterPeer))
            && afterPeer[3] != beforePeer[3] && ring0Progress != beforeRing0 && tasks.BootTicks() > beforeBoot, "peers survive");
        printf((char*)"SYNC REAP free="); printfHex32(frames.getStatistics().freeFrames);
        printf((char*)" expected="); printfHex32(survivorBaseline); printf((char*)"\n");
    }
    Require(injectedClockErrors == 1, "one real clock failure exercised");
    Require(runtime.RequestExit(peer, 0) && runtime.Reap() == 1
        && frames.getStatistics().freeFrames == baseline, "exact final baseline");
    Require(!runtime.FpStatistics().invariantFailures, "FP ownership preserved");
    printf((char*)"SYNC FINAL free="); printfHex32(frames.getStatistics().freeFrames);
    printf((char*)" expected="); printfHex32(baseline); printf((char*)"\n");
    Finish(true);
}
