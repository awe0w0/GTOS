#define NativeProcessSmoke NativeDiagUnusedBaseline
#include "tests/native_process_smoke.cpp"
#undef NativeProcessSmoke
#include "record.h"

namespace {
    uint8_t diagElf[1048576];
    const uint32_t RecordAddress = 0x40100000U;
    void DiagFinish(bool pass) {
        printf(pass ? (char*)"NATIVE LOW LEVEL ALLOC PASS\n" : (char*)"NATIVE LOW LEVEL ALLOC FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void DiagRequire(bool value, const char* why) {
        if (!value) { printf((char*)"FAILED LOW LEVEL ALLOC "); printf((char*)why); printf((char*)"\n"); DiagFinish(false); }
    }
    NativeDiagRecord Read(NativeRuntime& runtime, uint32_t id) {
        NativeDiagRecord record = {};
        DiagRequire(runtime.ReadMemory(id, RecordAddress, &record, sizeof(record)), "full CPL3 record");
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
        DiagRequire(!(seen[0x80000000U >> 27] & (1U << ((0x80000000U >> 22) & 31))), "dynamic table absent in ELF");
        return plan.pageCount + 2 + tables + 1;
    }
    uint32_t Spawn(NativeRuntime& runtime, const MultibootModule& module, uint32_t mode, uint32_t& cost) {
        DiagRequire(module.end > module.start && module.end - module.start <= sizeof(diagElf), "bounded ELF input");
        const uint32_t bytes = module.end - module.start;
        Elf32LoadPlan plan;
        DiagRequire(ValidateElf32((const uint8_t*)module.start, bytes, plan), "real ELF validation");
        for (uint32_t i = 0; i < bytes; ++i) diagElf[i] = ((const uint8_t*)module.start)[i];
        bool found = false;
        for (uint32_t i = 0; i < plan.segmentCount; ++i) {
            const Elf32LoadSegment& segment = plan.segments[i];
            if (RecordAddress < segment.virtualAddress || RecordAddress - segment.virtualAddress > segment.fileSize
                || segment.fileSize - (RecordAddress - segment.virtualAddress) < sizeof(NativeDiagRecord)) continue;
            uint8_t* record = diagElf + segment.fileOffset + RecordAddress - segment.virtualAddress;
            DiagRequire(Get32(record) == 1 && Get32(record + 4) == 0, "fresh initial state");
            Put32(record + 4, mode); found = true;
        }
        DiagRequire(found, "file backed record");
        uint32_t id = 0;
        DiagRequire(runtime.CreateElf(diagElf, bytes, id) && id, "production CPL3 admission");
        cost = Cost(plan); return id;
    }
    NativeStatus Wait(NativeRuntime& runtime, TaskManager& tasks, uint32_t id, uint32_t mode) {
        const uint32_t begin = tasks.Ticks();
        for (;;) {
            NativeStatus status = {};
            DiagRequire(runtime.Status(id, status), "status retained");
            if (!status.live) return status;
            if (mode == 4 && Read(runtime, id).progress >= 3) {
                DiagRequire(runtime.RequestExit(id, 77), "cancel live allocator arena");
                DiagRequire(runtime.Status(id, status) && !status.live, "cancel stopped");
                return status;
            }
            DiagRequire((uint32_t)(tasks.Ticks() - begin) < 2000, "bounded guest deadline");
            WaitTicks(tasks, 1);
        }
    }
}
asm(".section .text.native_diag_integer_peer,\"ax\"\n.balign 16\n"
    ".global native_user_start,native_user_end\nnative_user_start:\n"
    "incl 0x4000200c\njmp native_user_start\nnative_user_end:\n.text\n");
extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    printf((char*)"NATIVE LOW LEVEL ALLOC BOOT\n");
    GlobalDescriptorTable gdt;
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    DiagRequire(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frame bootstrap");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end, (const MultibootInfo*)multiboot, 0, 0};
    DiagRequire(paging.prepareIdentity(frames, config), "identity template");
    NativeRuntime runtime;
    DiagRequire(runtime.PrepareStacks(paging, frames), "guarded kernel stacks");
    DiagRequire(paging.enable() && paging.sealForSharedProcessors(), "sealed template");
    DiagRequire(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2), "native runtime with user SSE2");
    const uint32_t kernelDirectory = paging.getStatistics().directoryAddress;
    const uint32_t baseline = frames.getStatistics().freeFrames;
    Task ring0(&gdt, Ring0Task);
    DiagRequire(tasks.AddTask(&ring0), "ring0 survivor");
    const MultibootInfo* boot = (const MultibootInfo*)multiboot;
    DiagRequire((boot->flags & 8) && boot->moduleCount == 1, "one actual native ELF");
    const MultibootModule* modules = (const MultibootModule*)boot->modules;
    const uint32_t peer = Create(runtime, 0x11223344U, 0);
    const uint32_t survivorBaseline = frames.getStatistics().freeFrames;
    DiagRequire(baseline - survivorBaseline == 7, "peer frame cost");
    asm volatile("outb %0,$0x43" : : "a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(11931 & 255)));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(11931 >> 8)));
    interrupts.Activate();
    const uint32_t modes[] = {0,1,2,3,4,0};
    for (uint32_t n = 0; n < sizeof(modes) / 4; ++n) {
        uint32_t beforePeer[4], afterPeer[4];
        DiagRequire(runtime.ReadMemory(peer, NativeRuntime::DataAddress, beforePeer, sizeof(beforePeer)), "peer before");
        const uint32_t beforeWritten = runtime.Statistics().writtenBytes;
        const uint32_t beforeRing0 = ring0Progress, beforeBoot = tasks.BootTicks();
        uint32_t cost = 0;
        const uint32_t id = Spawn(runtime, modules[0], modes[n], cost);
        const NativeStatus status = Wait(runtime, tasks, id, modes[n]);
        const NativeDiagRecord record = Read(runtime, id);
        printf((char*)"LLA OBSERVE error="); printfHex32(record.error);
        printf((char*)" checks="); printfHex32(record.checks);
        printf((char*)" groups="); printfHex32(record.textBytes);
        printf((char*)" exit="); printfHex32(status.exitCode);
        printf((char*)" vector="); printfHex32(status.faultVector); printf((char*)"\n");
        DiagRequire(status.observedCs == 0x23 && status.observedCr3 == status.directory
            && status.directory != kernelDirectory, "observed private CPL3");
        DiagRequire(record.version == 1 && record.mode == modes[n] && record.error == 0
            && record.checks >= 5 && record.region && record.base == 0x80000000U
            && record.length == 4096 && record.reserved[0] == 0x80001000U && record.reserved[1] == 0
            && record.reserved[2] == 0 && record.reserved[3] == 0, "validated record and resident region");
        uint32_t marker = 0;
        DiagRequire(runtime.ReadMemory(id, record.base, &marker, sizeof(marker)) && marker == 0xC011CA11U, "callback resources retained");
        const uint32_t retainedCost = cost + 50 + (modes[n] == 4 ? 32U : 0U);
        DiagRequire(record.checks == 91 && record.textBytes == 255 && !status.faultVector,
            "eight allocator and VM rollback groups completed without CPU fault");
        if (modes[n] == 0) DiagRequire(record.stage == 2 && !status.exitCode
            && !record.progress && !record.formattedBytes && !record.expectedBytes, "normal return");
        else if (modes[n] == 4) {
            DiagRequire(record.stage == 3 && record.progress >= 3 && status.exitCode == 77
                && record.formattedBytes >= 0x80031000U
                && record.expectedBytes == record.formattedBytes + 60016U, "cancel two retained chunks");
            uint32_t first = 0, second = 0;
            DiagRequire(runtime.ReadMemory(id, record.formattedBytes, &first, sizeof(first))
                && runtime.ReadMemory(id, record.expectedBytes, &second, sizeof(second))
                && first == 0x66666666U && second == 0x77777777U, "cancelled payload retained until reap");
        } else DiagRequire(record.stage == 2 && status.exitCode == 0x48000021U
            && !record.progress && !record.formattedBytes && !record.expectedBytes,
            "allocator boundary terminates via actual native abort");
        const uint32_t written = runtime.Statistics().writtenBytes - beforeWritten;
        DiagRequire((modes[n] >= 1 && modes[n] <= 3) ? written > 23 && written < 1024 : written == 0,
            "bounded fatal diagnostic or silent normal execution");
        DiagRequire(survivorBaseline - frames.getStatistics().freeFrames == retainedCost, "ELF plus marker TLS default and metadata arenas retained");
        printf((char*)"LLA CASE id="); printfHex32(id); printf((char*)" mode="); printfHex32(modes[n]);
        printf((char*)" cs="); printfHex32(status.observedCs); printf((char*)" cr3="); printfHex32(status.directory);
        printf((char*)" exit="); printfHex32(status.exitCode); printf((char*)" vector="); printfHex32(status.faultVector);
        printf((char*)" cost="); printfHex32(retainedCost);
        printf((char*)" written="); printfHex32(written);
        printf((char*)" fault="); printfHex32(status.faultAddress); printf((char*)" fault_error="); printfHex32(status.faultError); printf((char*)" words= ");
        const uint32_t* words = (const uint32_t*)&record;
        for (uint32_t i = 0; i < 16; ++i) { printfHex32(words[i]); printf((char*)" "); }
        printf((char*)"\n");
        DiagRequire(runtime.Reap() == 1 && frames.getStatistics().freeFrames == survivorBaseline, "exact victim reap");
        WaitTicks(tasks, 6);
        NativeStatus peerStatus = {};
        DiagRequire(runtime.Status(peer, peerStatus) && peerStatus.live && !peerStatus.systemCalls
            && runtime.ReadMemory(peer, NativeRuntime::DataAddress, afterPeer, sizeof(afterPeer))
            && afterPeer[3] != beforePeer[3] && ring0Progress != beforeRing0 && tasks.BootTicks() > beforeBoot,
            "integer ring0 and boot peers survive");
        printf((char*)"LLA REAP free="); printfHex32(frames.getStatistics().freeFrames);
        printf((char*)" expected="); printfHex32(survivorBaseline); printf((char*)"\n");
    }
    DiagRequire(runtime.RequestExit(peer, 0) && runtime.Reap() == 1
        && frames.getStatistics().freeFrames == baseline, "exact final baseline");
    DiagRequire(!runtime.FpStatistics().invariantFailures, "FP ownership preserved");
    printf((char*)"LLA FINAL free="); printfHex32(frames.getStatistics().freeFrames);
    printf((char*)" expected="); printfHex32(baseline); printf((char*)"\n");
    DiagFinish(true);
}
