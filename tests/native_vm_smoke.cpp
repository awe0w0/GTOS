// Reuse boot/checked-copy helpers; the old acceptance entry is discarded by GC
// and is never called. Every VM case below admits an independently built ELF.
#define NativeProcessSmoke NativeVmUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include <process/vm_abi.h>

namespace {
    const uint32_t VmRecordAddress = 0x40020000U;
    struct VmRecord { uint32_t version, mode, stage, base, handle, foreignHandle; };
    static_assert(sizeof(VmRecord) == 24, "VM record wire");
    uint8_t vmElfCopy[65536], vmPages[3 * 4096];
    void VmFinish(bool pass) __attribute__((noreturn));
    void VmFinish(bool pass) {
        printf(pass ? (char*)"NATIVE VM SMOKE PASS\n" : (char*)"NATIVE VM SMOKE FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void VmRequire(bool value, const char* why) {
        if (!value) { printf((char*)"FAILED VM "); printf((char*)why); printf((char*)"\n"); VmFinish(false); }
    }
    bool VmAll(const uint8_t* bytes, uint32_t length, uint8_t value) {
        for (uint32_t i = 0; i < length; ++i) if (bytes[i] != value) return false;
        return true;
    }
    VmRecord VmReadRecord(NativeRuntime& runtime, uint32_t id) {
        VmRecord record = {};
        VmRequire(runtime.ReadMemory(id, VmRecordAddress, &record, sizeof(record)), "read actual ELF probe record");
        return record;
    }
    VmRecord VmWaitStage(NativeRuntime& runtime, TaskManager& tasks, uint32_t id, uint32_t stage) {
        const uint32_t begin = tasks.Ticks();
        VmRecord record = {};
        for (;;) {
            record = VmReadRecord(runtime, id);
            if (record.version == 1 && record.stage >= stage) return record;
            NativeStatus status = {};
            VmRequire(runtime.Status(id, status) && status.live, "probe alive until announced stage");
            VmRequire((uint32_t)(tasks.Ticks() - begin) < 800, "probe reaches bounded stage deadline");
            WaitTicks(tasks, 1);
        }
    }
    uint32_t VmStaticFrameCost(const Elf32LoadPlan& plan) {
        uint32_t seen[32], tables = 0;
        for (uint32_t i = 0; i < 32; ++i) seen[i] = 0;
        for (uint32_t i = 0; i < plan.segmentCount; ++i) {
            const Elf32LoadSegment& segment = plan.segments[i];
            if (!segment.memorySize) continue;
            const uint32_t last = (segment.virtualAddress + segment.memorySize - 1) >> 22;
            for (uint32_t index = segment.virtualAddress >> 22; index <= last; ++index) {
                const uint32_t mask = 1U << (index & 31);
                if (!(seen[index >> 5] & mask)) { seen[index >> 5] |= mask; ++tables; }
            }
        }
        const uint32_t stackIndex = NativeRuntime::UserStackBottom >> 22;
        if (!(seen[stackIndex >> 5] & (1U << (stackIndex & 31)))) ++tables;
        return plan.pageCount + 2 + tables + 1; // Static data/code, stack, tables and directory.
    }
    uint32_t VmResidentFrameCost(uint32_t base, uint32_t mode) {
        if (mode == 4) return 0; // Released region owns neither data nor table.
        const uint32_t pages = mode == 3 || mode == 5 ? 2 : 3;
        const uint32_t first = mode == 3 ? base + 4096 : base;
        const uint32_t last = mode == 5 ? base + 4096 : base + 8192;
        return pages + ((first >> 22) == (last >> 22) ? 1 : 2);
    }
    uint32_t VmCreateElf(NativeRuntime& runtime, const MultibootModule& module, uint32_t foreignHandle,
                         uint32_t* staticFrameCost = 0) {
        const uint32_t bytes = module.end - module.start;
        VmRequire(module.end >= module.start && bytes && bytes <= sizeof(vmElfCopy), "bounded native ELF <=64KiB");
        const uint8_t* source = (const uint8_t*)module.start;
        Elf32LoadPlan plan;
        VmRequire(ValidateElf32(source, bytes, plan) && plan.pageCount <= ProcessAddressSpace::MaximumPages - 2,
            "actual ELF32 load/total-page admission");
        bool patched = false;
        for (uint32_t i = 0; i < bytes; ++i) vmElfCopy[i] = source[i];
        for (uint32_t i = 0; i < plan.segmentCount; ++i) {
            const Elf32LoadSegment& segment = plan.segments[i];
            if (VmRecordAddress < segment.virtualAddress
                || VmRecordAddress - segment.virtualAddress > segment.fileSize
                || segment.fileSize - (VmRecordAddress - segment.virtualAddress) < sizeof(VmRecord)) continue;
            const uint32_t offset = segment.fileOffset + VmRecordAddress - segment.virtualAddress;
            VmRequire(Get32(vmElfCopy + offset) == 1, "serialized probe record version");
            Put32(vmElfCopy + offset + 20, foreignHandle); patched = true;
        }
        VmRequire(patched, "record resides in initialized ELF data");
        uint32_t id = 0;
        VmRequire(runtime.CreateElf(vmElfCopy, bytes, id) && id, "admit independent VM ELF through real runtime");
        if (staticFrameCost) *staticFrameCost = VmStaticFrameCost(plan);
        return id;
    }
    void VmPrivateContext(const NativeStatus& status, uint32_t kernelDirectory) {
        VmRequire(status.observedCs == 0x23 && status.observedCr3 == status.directory
            && status.directory != kernelDirectory && status.systemCalls,
            "actual CPL3 selector/private CR3 and production int80 calls");
    }
    void VmPatterns(NativeRuntime& runtime, uint32_t id, uint32_t base) {
        VmRequire(runtime.ReadMemory(id, base, vmPages, 8192), "read actual resident VM pages in kernel");
        VmRequire(VmAll(vmPages, 4096, 0) && VmAll(vmPages + 4096, 4096, 0x5A),
            "all 8192 VM bytes match independent zero/pattern oracle");
    }
    void VmV8Patterns(NativeRuntime& runtime, uint32_t id, uint32_t base, uint32_t mode) {
        VmRequire(runtime.ReadMemory(id, base, vmPages, sizeof(vmPages)), "read all actual V8 resident pages");
        const uint32_t finalSeeds[] = {0x99, 0x5A, 0xA6};
        const uint32_t initialSeeds[] = {0x31, 0x72, 0xC4};
        const uint32_t* seeds = mode == 6 ? initialSeeds : finalSeeds;
        for (uint32_t page = 0; page < 3; ++page)
            for (uint32_t byte = 0; byte < 4096; ++byte)
                VmRequire(vmPages[page * 4096 + byte] == (uint8_t)(byte ^ seeds[page]),
                    "all 12288 V8 bytes match independent upstream consumer oracle");
    }
    void VmSurvivors(NativeRuntime& runtime, TaskManager& tasks, uint32_t cpuPeer, uint32_t vmPeer,
                     uint32_t beforeCpu, uint32_t beforeYield, uint32_t beforeRing0, uint32_t beforeBoot) {
        WaitTicks(tasks, 10);
        uint32_t data[4]; NativeStatus peer = {};
        VmRequire(runtime.ReadMemory(cpuPeer, NativeRuntime::DataAddress, data, sizeof(data))
            && data[0] == 0x11223344 && data[3] != beforeCpu,
            "CPU-bound native peer continues through VM test");
        VmRequire(runtime.Status(vmPeer, peer) && peer.live && peer.statistics.yields > beforeYield
            && ring0Progress != beforeRing0 && tasks.BootTicks() > beforeBoot,
            "resident VM peer, ring0 and boot continue after victim");
    }
}
// Tiny integer-only CPU-bound peer, copied as the old helper's NativeImage.
// It has no syscall/yield, so its progress proves actual timer preemption.
asm(".section .text.native_vm_peer,\"ax\"\n.balign 16\n"
    ".global native_user_start,native_user_end\nnative_user_start:\n"
    "incl 0x4000200c\njmp native_user_start\nnative_user_end:\n.text\n");

extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    printf((char*)"NATIVE VM SMOKE BOOT\n");
    GlobalDescriptorTable gdt;
    VmRequire(gdt.CodeSegmentSelector() == 0x10 && gdt.DataSegmentSelector() == 0x18,
        "kernel selectors preserved");
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    VmRequire(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frames initialize");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end,
        (const MultibootInfo*)multiboot, 0, 0};
    VmRequire(paging.prepareIdentity(frames, config), "identity prepare");
    NativeRuntime runtime;
    VmRequire(runtime.PrepareStacks(paging, frames), "retained guard kernel stacks");
    VmRequire(paging.enable() && paging.sealForSharedProcessors(), "enabled frozen kernel template");
    VmRequire(runtime.Activate(tasks, gdt, paging, frames), "activate actual BSP CPL3 runtime");
    const uint32_t kernelDirectory = paging.getStatistics().directoryAddress;
    const uint32_t baseline = frames.getStatistics().freeFrames;
    Task ring0(&gdt, Ring0Task); VmRequire(tasks.AddTask(&ring0), "independent ring0 peer");
    const MultibootInfo* boot = (const MultibootInfo*)multiboot;
    VmRequire((boot->flags & 8) && boot->moduleCount >= 7, "seven independent raw VM ELF modules");
    const MultibootModule* modules = (const MultibootModule*)boot->modules;
    const uint32_t cpuPeer = Create(runtime, 0x11223344, 0);
    uint32_t peerStaticFrames = 0;
    const uint32_t vmPeer = VmCreateElf(runtime, modules[6], 0, &peerStaticFrames);
    // 100 Hz PIT: only actual IRQs account ticks, never VM/yield syscalls.
    asm volatile("outb %0,$0x43" : : "a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(11932 & 255)));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(11932 >> 8)));
    interrupts.Activate();
    const VmRecord peerRecord = VmWaitStage(runtime, tasks, vmPeer, 1);
    VmRequire(peerRecord.mode == 6 && peerRecord.base == 0x81000000U && peerRecord.handle,
        "real peer reservation/three resident pages at unsigned high VA");
    VmPatterns(runtime, vmPeer, peerRecord.base);
    NativeStatus peerState = {}; runtime.Status(vmPeer, peerState); VmPrivateContext(peerState, kernelDirectory);
    const uint32_t survivorBaseline = frames.getStatistics().freeFrames;
    VmRequire(baseline - survivorBaseline == 7 + peerStaticFrames + VmResidentFrameCost(peerRecord.base, 6),
        "16MiB peer reservation consumes only static admission plus three resident pages/table");
    for (uint32_t moduleIndex = 0; moduleIndex < boot->moduleCount; ++moduleIndex) {
        uint32_t beforeData[4]; NativeStatus beforePeer = {};
        VmRequire(runtime.ReadMemory(cpuPeer, NativeRuntime::DataAddress, beforeData, sizeof(beforeData))
            && runtime.Status(vmPeer, beforePeer), "snapshot independent survivors");
        const uint32_t beforeRing0 = ring0Progress, beforeBoot = tasks.BootTicks();
        uint32_t staticFrames = 0;
        const uint32_t id = VmCreateElf(runtime, modules[moduleIndex], peerRecord.handle, &staticFrames);
        NativeStatus status = {};
        VmRecord record = VmWaitStage(runtime, tasks, id, 1);
        const bool v8 = moduleIndex >= 7;
        VmRequire(record.version == 1 && record.mode <= 6 && (v8 ? record.base >= ProcessAddressSpace::DynamicBase
            && record.base < ProcessAddressSpace::DynamicLimit : record.base == peerRecord.base)
            && record.handle && record.handle != peerRecord.handle, "same VA, distinct globally owned handles");
        if (record.mode == 6) {
            if (v8) VmV8Patterns(runtime, id, record.base, record.mode); else VmPatterns(runtime, id, record.base);
            VmRequire(runtime.RequestExit(id, 73), "external RequestExit with live VM frames");
            VmRequire(runtime.Status(id, status) && !status.live && status.exitCode == 73 && !status.faultVector,
                "RequestExit stopped real resident native process");
        } else {
            status = WaitStopped(runtime, tasks, id);
            record = VmReadRecord(runtime, id);
            if (!record.mode) {
                VmRequire(record.stage == 2 && status.exitCode == 0 && !status.faultVector,
                    "real wire/adversarial positive probe completes and exits normally");
                if (v8) VmV8Patterns(runtime, id, record.base, record.mode); else VmPatterns(runtime, id, record.base);
            } else {
                const uint32_t address = record.base + (record.mode == 5 ? 8192U : 0U);
                const uint32_t bits = record.mode == 1 ? 7U : (v8 && record.mode == 2 ? 6U : 4U);
                VmRequire(status.exitCode == (0x80000000U | 14) && status.faultVector == 14
                    && status.faultAddress == address && (status.faultError & 7) == bits,
                    "actual cached mapping transition produces exact user PF address/bits");
            }
        }
        VmRequire(survivorBaseline - frames.getStatistics().freeFrames
            == staticFrames + VmResidentFrameCost(record.base, record.mode),
            "actual retained VM/data/table frame count matches stage and operation");
        VmPrivateContext(status, kernelDirectory);
        printf(v8 ? (char*)"V8 VM CASE mode=" : (char*)"RAW VM CASE mode="); printfHex32(record.mode); printf((char*)" cs="); printfHex32(status.observedCs);
        printf((char*)" cr3="); printfHex32(status.observedCr3); printf((char*)" cr2="); printfHex32(status.faultAddress);
        printf((char*)" error="); printfHex32(status.faultError); printf((char*)"\n");
        VmRequire(runtime.Reap() == 1 && frames.getStatistics().freeFrames == survivorBaseline,
            "fault/exit/cancel deferred reap restores exact private frame baseline");
        VmPatterns(runtime, vmPeer, peerRecord.base);
        VmSurvivors(runtime, tasks, cpuPeer, vmPeer, beforeData[3], beforePeer.statistics.yields, beforeRing0, beforeBoot);
    }
    VmRequire(runtime.RequestExit(vmPeer, 74) && runtime.RequestExit(cpuPeer, 0), "stop final VM and CPU peers");
    VmRequire(runtime.Reap() == 2 && frames.getStatistics().freeFrames == baseline, "complete original allocator baseline restored");
    WaitTicks(tasks, 10); VmRequire(ring0Progress && tasks.BootTicks(), "ring0/boot remain runnable after all VM reaping");
    VmFinish(true);
}
