#define NativeProcessSmoke NativeTimeFormatUnusedBaseline
#include "tests/native_process_smoke.cpp"
#undef NativeProcessSmoke
#include "record.h"
#include "cases-count.h"

namespace {
    uint8_t time_formatElf[1048576];
    const uint32_t RecordAddress = 0x40100000U;
    void TimeFormatFinish(bool pass) {
        printf(pass ? (char*)"NATIVE TIME_FORMAT SMOKE PASS\n" : (char*)"NATIVE TIME_FORMAT SMOKE FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void TimeFormatRequire(bool value, const char* why) {
        if (!value) { printf((char*)"FAILED TIME_FORMAT "); printf((char*)why); printf((char*)"\n"); TimeFormatFinish(false); }
    }
    NativeTimeFormatRecord Read(NativeRuntime& runtime, uint32_t id) {
        NativeTimeFormatRecord record = {};
        TimeFormatRequire(runtime.ReadMemory(id, RecordAddress, &record, sizeof(record)), "full CPL3 record");
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
        TimeFormatRequire(!(seen[0x80000000U >> 27] & (1U << ((0x80000000U >> 22) & 31))), "dynamic table absent in ELF");
        return plan.pageCount + 2 + tables + 1;
    }
    uint32_t Spawn(NativeRuntime& runtime, const MultibootModule& module, uint32_t mode, uint32_t& cost) {
        TimeFormatRequire(module.end > module.start && module.end - module.start <= sizeof(time_formatElf), "bounded ELF input");
        const uint32_t bytes = module.end - module.start;
        Elf32LoadPlan plan;
        TimeFormatRequire(ValidateElf32((const uint8_t*)module.start, bytes, plan), "real ELF validation");
        for (uint32_t i = 0; i < bytes; ++i) time_formatElf[i] = ((const uint8_t*)module.start)[i];
        bool found = false;
        for (uint32_t i = 0; i < plan.segmentCount; ++i) {
            const Elf32LoadSegment& segment = plan.segments[i];
            if (RecordAddress < segment.virtualAddress || RecordAddress - segment.virtualAddress > segment.fileSize
                || segment.fileSize - (RecordAddress - segment.virtualAddress) < sizeof(NativeTimeFormatRecord)) continue;
            uint8_t* record = time_formatElf + segment.fileOffset + RecordAddress - segment.virtualAddress;
            TimeFormatRequire(Get32(record) == 1 && Get32(record + 4) == 0, "fresh initial state");
            Put32(record + 4, mode); found = true;
        }
        TimeFormatRequire(found, "file backed record");
        uint32_t id = 0;
        TimeFormatRequire(runtime.CreateElf(time_formatElf, bytes, id) && id, "production CPL3 admission");
        cost = Cost(plan); return id;
    }
    NativeStatus Wait(NativeRuntime& runtime, TaskManager& tasks, uint32_t id, uint32_t mode) {
        const uint32_t begin = tasks.Ticks();
        for (;;) {
            NativeStatus status = {};
            TimeFormatRequire(runtime.Status(id, status), "status retained");
            if (!status.live) return status;
            if (mode == 4 && Read(runtime, id).cycles >= 3) {
                TimeFormatRequire(runtime.RequestExit(id, 73), "cancel pending callback");
                TimeFormatRequire(runtime.Status(id, status) && !status.live, "cancel stopped");
                return status;
            }
            TimeFormatRequire((uint32_t)(tasks.Ticks() - begin) < 2000, "bounded guest deadline");
            WaitTicks(tasks, 1);
        }
    }
}
asm(".section .text.native_time_format_integer_peer,\"ax\"\n.balign 16\n"
    ".global native_user_start,native_user_end\nnative_user_start:\n"
    "incl 0x4000200c\njmp native_user_start\nnative_user_end:\n.text\n");
extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    printf((char*)"NATIVE TIME_FORMAT SMOKE BOOT\n");
    GlobalDescriptorTable gdt;
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    TimeFormatRequire(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frame bootstrap");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end, (const MultibootInfo*)multiboot, 0, 0};
    TimeFormatRequire(paging.prepareIdentity(frames, config), "identity template");
    NativeRuntime runtime;
    TimeFormatRequire(runtime.PrepareStacks(paging, frames), "guarded kernel stacks");
    TimeFormatRequire(paging.enable() && paging.sealForSharedProcessors(), "sealed template");
    TimeFormatRequire(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2), "native runtime with user SSE2");
    const uint32_t kernelDirectory = paging.getStatistics().directoryAddress;
    const uint32_t baseline = frames.getStatistics().freeFrames;
    Task ring0(&gdt, Ring0Task);
    TimeFormatRequire(tasks.AddTask(&ring0), "ring0 survivor");
    const MultibootInfo* boot = (const MultibootInfo*)multiboot;
    TimeFormatRequire((boot->flags & 8) && boot->moduleCount == 1, "one actual native ELF");
    const MultibootModule* modules = (const MultibootModule*)boot->modules;
    const uint32_t peer = Create(runtime, 0x11223344U, 0);
    const uint32_t survivorBaseline = frames.getStatistics().freeFrames;
    TimeFormatRequire(baseline - survivorBaseline == 7, "peer frame cost");
    asm volatile("outb %0,$0x43" : : "a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(11931 & 255)));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(11931 >> 8)));
    interrupts.Activate();
    const uint32_t modes[] = {0,1,2,3,4,0};
    for (uint32_t n = 0; n < sizeof(modes) / 4; ++n) {
        uint32_t beforePeer[4], afterPeer[4];
        TimeFormatRequire(runtime.ReadMemory(peer, NativeRuntime::DataAddress, beforePeer, sizeof(beforePeer)), "peer before");
        const uint32_t beforeRing0 = ring0Progress, beforeBoot = tasks.BootTicks();
        uint32_t cost = 0;
        const uint32_t id = Spawn(runtime, modules[0], modes[n], cost);
        const NativeStatus status = Wait(runtime, tasks, id, modes[n]);
        const NativeTimeFormatRecord record = Read(runtime, id);
        TimeFormatRequire(status.observedCs == 0x23 && status.observedCr3 == status.directory
            && status.directory != kernelDirectory, "observed private CPL3");
        printf((char*)"TIME_FORMAT CASE id="); printfHex32(id); printf((char*)" mode="); printfHex32(modes[n]);
        printf((char*)" cs="); printfHex32(status.observedCs); printf((char*)" cr3="); printfHex32(status.directory);
        printf((char*)" exit="); printfHex32(status.exitCode); printf((char*)" vector="); printfHex32(status.faultVector);
        printf((char*)" cost="); printfHex32(cost + 2); printf((char*)" words= ");
        const uint32_t* words = (const uint32_t*)&record;
        for (uint32_t i = 0; i < 16; ++i) { printfHex32(words[i]); printf((char*)" "); }
        printf((char*)"\n");
        TimeFormatRequire(record.version == 1 && record.mode == modes[n] && record.error == 0
            && record.checks >= 5 && record.region && record.base == 0x80000000U
            && record.length == 4096 && record.reserved == 0, "validated record and resident region");
        uint32_t marker = 0;
        TimeFormatRequire(runtime.ReadMemory(id, record.base, &marker, sizeof(marker)) && marker == 0xC011CA11U, "callback resources retained");
        int data[9] = {};
        const int expected[9] = {5,7,23,29,1,124,4,59,0};
        TimeFormatRequire(record.pointer >= RecordAddress && record.objectBytes == 36 && record.heapEmpty == 1
            && runtime.ReadMemory(id, record.pointer, data, sizeof(data)), "actual supplied tm retained without heap");
        for (uint32_t i = 0; i < 9; ++i) TimeFormatRequire(data[i] == expected[i], "caller tm is unchanged");
        if (modes[n] == 0) TimeFormatRequire(record.stage == 2 && record.checks >= 100
            && record.masks == ExpectedFormatCases && record.rejected == ExpectedCapacityCases
            && record.calls == ExpectedFormatCases + ExpectedCapacityCases + 7 && record.cycles == 0
            && !status.exitCode && !status.faultVector, "real formatting dates capacities and guarded writes");
        else {
            TimeFormatRequire(record.stage == 1 && record.checks == 7 && record.masks == 0
                && record.rejected == 0, "bounded formatting fault or cancellation state");
            if (modes[n] <= 3) TimeFormatRequire(record.cycles == 0 && record.calls == 1
                && status.exitCode == (0x80000000U | 14) && status.faultVector == 14
                && (status.faultError & 4) == 4 && status.faultAddress == 0xBFFFCFF0U,
                "invalid destination format or tm pointer fault contained");
            else TimeFormatRequire(record.cycles >= 3 && record.calls >= 3 && status.exitCode == 73
                && !status.faultVector, "cancel real repeated formatting");
        }
        TimeFormatRequire(survivorBaseline - frames.getStatistics().freeFrames == cost + 2, "ELF plus one resident page and one table retained");
        TimeFormatRequire(runtime.Reap() == 1 && frames.getStatistics().freeFrames == survivorBaseline, "exact victim reap");
        WaitTicks(tasks, 6);
        NativeStatus peerStatus = {};
        TimeFormatRequire(runtime.Status(peer, peerStatus) && peerStatus.live && !peerStatus.systemCalls
            && runtime.ReadMemory(peer, NativeRuntime::DataAddress, afterPeer, sizeof(afterPeer))
            && afterPeer[3] != beforePeer[3] && ring0Progress != beforeRing0 && tasks.BootTicks() > beforeBoot,
            "integer ring0 and boot peers survive");
        printf((char*)"TIME_FORMAT REAP free="); printfHex32(frames.getStatistics().freeFrames);
        printf((char*)" expected="); printfHex32(survivorBaseline); printf((char*)"\n");
    }
    TimeFormatRequire(runtime.RequestExit(peer, 0) && runtime.Reap() == 1
        && frames.getStatistics().freeFrames == baseline, "exact final baseline");
    TimeFormatRequire(!runtime.FpStatistics().invariantFailures, "FP ownership preserved");
    printf((char*)"TIME_FORMAT FINAL free="); printfHex32(frames.getStatistics().freeFrames);
    printf((char*)" expected="); printfHex32(baseline); printf((char*)"\n");
    TimeFormatFinish(true);
}
