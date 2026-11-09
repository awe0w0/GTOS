// Reuse the actual boot/runtime helpers. The old acceptance entry is discarded
// by section GC; only this observer drives the newly compiled native modules.
#define NativeProcessSmoke PerfettoMemoryUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include "../apps/native_content_build/gtos-platform/probe/memory_record.h"

namespace {
    uint8_t memoryElf[65536];
    void MemoryFinish(bool pass) __attribute__((noreturn));
    void MemoryFinish(bool pass) {
        printf(pass ? (char*)"PERFETTO MEMORY GUEST PASS\n" : (char*)"PERFETTO MEMORY GUEST FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void MemoryRequire(bool value, const char* reason) {
        if (!value) {
            printf((char*)"PMEM FAILED "); printf((char*)reason); printf((char*)"\n");
            MemoryFinish(false);
        }
    }
}

extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    printf((char*)"PERFETTO MEMORY GUEST BOOT\n");
    GlobalDescriptorTable gdt;
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    MemoryRequire(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frames");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end,
        (const MultibootInfo*)multiboot, 0, 0};
    MemoryRequire(paging.prepareIdentity(frames, config), "identity");
    NativeRuntime runtime;
    MemoryRequire(runtime.PrepareStacks(paging, frames), "guarded kernel stacks");
    MemoryRequire(paging.enable() && paging.sealForSharedProcessors(), "frozen paging template");
    MemoryRequire(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2), "native SSE2 activation");
    printf((char*)"PMEM PROFILE SSE2\n");
    const uint32_t baseline = frames.getStatistics().freeFrames;
    Task ring0(&gdt, Ring0Task);
    MemoryRequire(tasks.AddTask(&ring0), "independent kernel task");
    const MultibootInfo* boot = (const MultibootInfo*)multiboot;
    MemoryRequire((boot->flags & 8) && boot->moduleCount == 5, "five current-source modules");
    const MultibootModule* modules = (const MultibootModule*)boot->modules;
    asm volatile("outb %0,$0x43" : : "a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(11932 & 255)));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(11932 >> 8)));
    interrupts.Activate();
    for (uint32_t mode = 0; mode < 5; ++mode) {
        const MultibootModule& module = modules[mode];
        MemoryRequire(module.end > module.start && module.end - module.start <= sizeof(memoryElf), "bounded ELF");
        const uint32_t bytes = module.end - module.start;
        for (uint32_t index = 0; index < bytes; ++index) memoryElf[index] = ((const uint8_t*)module.start)[index];
        Elf32LoadPlan plan;
        MemoryRequire(ValidateElf32(memoryElf, bytes, plan), "native ELF plan");
        uint32_t id;
        const NativeElfLaunchProfile largeAllocation = {Elf32MaximumPages, 2, 512};
        MemoryRequire(mode == 0 ? runtime.CreateElf(memoryElf, bytes, id, largeAllocation)
                                : runtime.CreateElf(memoryElf, bytes, id), "actual ELF admission");
        NativeStatus status = WaitStopped(runtime, tasks, id);
        gtos_perfetto_probe::Record record = {};
        MemoryRequire(runtime.ReadMemory(id, gtos_perfetto_probe::RecordAddress, &record, sizeof(record)), "consumer record");
        MemoryRequire(record.version == 1 && record.mode == mode && !record.failures, "consumer result");
        MemoryRequire(status.observedCs == 0x23 && status.observedCr3 == status.directory
            && status.directory != paging.getStatistics().directoryAddress
            && status.systemCalls >= (mode == 3 ? 2U : 3U), "CPL3 private context");
        if (!mode) {
            MemoryRequire(record.stage == 2 && record.checks == 120 && status.exitCode == 0 && !status.faultVector,
                "normal lifetime and byte comparisons");
        } else {
            const uint32_t error = mode == 3 ? 4 : 6;
            MemoryRequire(record.stage == 1 && record.base >= ProcessAddressSpace::DynamicBase
                && record.base < ProcessAddressSpace::DynamicLimit, "announced native allocation");
            const uint32_t expected = record.base + (mode == 1 ? -4096U : mode == 2 ? 8192U : 0);
            MemoryRequire(record.fault_address == expected && status.exitCode == (0x80000000U | 14)
                && status.faultVector == 14 && status.faultAddress == expected && (status.faultError & 7) == error,
                "exact guard/uncommitted/released user page fault");
        }
        printf((char*)"PMEM CASE mode="); printfHex32(mode);
        printf((char*)" checks="); printfHex32(record.checks);
        printf((char*)" base="); printfHex32(record.base);
        printf((char*)" cs="); printfHex32(status.observedCs);
        printf((char*)" calls="); printfHex32(status.systemCalls);
        printf((char*)" exit="); printfHex32(status.exitCode);
        printf((char*)" fault="); printfHex32(status.faultAddress);
        printf((char*)" error="); printfHex32(status.faultError);
        printf((char*)"\n");
        MemoryRequire(runtime.Reap() == 1 && frames.getStatistics().freeFrames == baseline, "exact Reap frame restoration");
        printf((char*)"PMEM REAP mode="); printfHex32(mode);
        printf((char*)" free="); printfHex32(frames.getStatistics().freeFrames);
        printf((char*)" expected="); printfHex32(baseline); printf((char*)"\n");
    }
    WaitTicks(tasks, 10);
    MemoryRequire(ring0Progress && tasks.BootTicks(), "surviving kernel scheduling");
    MemoryFinish(true);
}
