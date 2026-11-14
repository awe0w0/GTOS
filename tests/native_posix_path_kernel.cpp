#define NativeProcessSmoke NativePathUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include "../apps/native_libxml_file/probe/path_record.h"
#include <process/native_files.h>
#include <storage/filestore.h>
#include <drivers/ata.h>
#include <process/clock_abi.h>

namespace {
    uint8_t pathElf[65536];
    void PathFinish(bool pass) __attribute__((noreturn));
    void PathFinish(bool pass) {
        printf(pass ? (char*)"NATIVE PATH GUEST PASS\n" : (char*)"NATIVE PATH GUEST FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void PathRequire(bool value, const char* reason) {
        if (!value) { printf((char*)"PATH FAILED "); printf((char*)reason); printf((char*)"\n"); PathFinish(false); }
    }
}

extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    printf((char*)"NATIVE PATH GUEST BOOT\n");
    const MultibootInfo* boot = (const MultibootInfo*)multiboot;
    const char* command = (boot->flags & 4) ? (const char*)boot->commandLine : 0;
    const uint32_t phase = Contains(command, "unmounted") ? 1 : Contains(command, "detached") ? 2 : 0;
    GlobalDescriptorTable gdt;
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    PathRequire(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frames");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end,
        (const MultibootInfo*)multiboot, 0, 0};
    PathRequire(paging.prepareIdentity(frames, config), "identity");
    gtos::drivers::AdvancedTechnologyAttachment disk(0x1f0, true);
    gtos::storage::FileStore store;
    if (!phase) {
        PathRequire(disk.Identify() && disk.SectorCount() == 2064, "dedicated ATA geometry");
        PathRequire(store.Format(&disk, 8, 2048) == 0 && store.Mount(&disk, 8, 2048) == 0, "scratch volume");
    }
    NativeFiles files(store);
    NativeRuntime runtime;
    if (phase != 2) PathRequire(runtime.AttachFiles(files), "explicit file endpoint");
    PathRequire(runtime.PrepareStacks(paging, frames), "guarded kernel stacks");
    PathRequire(paging.enable() && paging.sealForSharedProcessors(), "frozen paging template");
    PathRequire(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2), "native SSE2 activation");
    const uint32_t baseline = frames.getStatistics().freeFrames;
    Task ring0(&gdt, Ring0Task);
    PathRequire(tasks.AddTask(&ring0), "independent kernel task");
    PathRequire((boot->flags & 8) && boot->moduleCount == 1, "one current-source module");
    const MultibootModule* modules = (const MultibootModule*)boot->modules;
    asm volatile("outb %0,$0x43" : : "a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR & 255)));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR >> 8)));
    interrupts.Activate();
    const MultibootModule& module = modules[0];
    PathRequire(module.end > module.start && module.end - module.start <= sizeof(pathElf), "bounded ELF");
    const uint32_t bytes = module.end - module.start;
    for (uint32_t index = 0; index < bytes; ++index) pathElf[index] = ((const uint8_t*)module.start)[index];
    Elf32LoadPlan plan;
    PathRequire(ValidateElf32(pathElf, bytes, plan), "native ELF plan");
    for (uint32_t iteration = 0; iteration < (phase ? 1U : 2U); ++iteration) {
        const uint32_t before = files.Statistics().calls;
        uint32_t id;
        PathRequire(runtime.CreateElf(pathElf, bytes, id), "actual ELF admission");
        const NativeStatus status = WaitStopped(runtime, tasks, id);
        NativePathRecord record = {};
        PathRequire(runtime.ReadMemory(id, 0x40020000, &record, sizeof(record)), "consumer record");
        PathRequire(record.version == 1 && record.mode == (phase ? 1U : 0U) && record.stage == 2 &&
            record.checks == (phase ? 6U : 71U) && !record.failures && !status.exitCode && !status.faultVector,
            "consumer result");
        const uint32_t calls = files.Statistics().calls - before;
        PathRequire(status.observedCs == 0x23 && status.observedCr3 == status.directory &&
            status.directory != paging.getStatistics().directoryAddress && status.systemCalls > 3,
            "actual CPL3 calls");
        PathRequire(calls == (phase == 2 ? 0 : status.systemCalls - 3) &&
            (phase == 2 ? status.systemCalls == 9 : calls >= 6) &&
            record.raw_calls == (phase ? 0U : 11U) && store.OpenCount() == 0, "endpoint calls and closed handles");
        printf((char*)"PATH CASE phase="); printfHex32(phase);
        printf((char*)" iteration="); printfHex32(iteration);
        printf((char*)" checks="); printfHex32(record.checks);
        printf((char*)" cs="); printfHex32(status.observedCs);
        printf((char*)" calls="); printfHex32(status.systemCalls);
        printf((char*)" service="); printfHex32(calls);
        printf((char*)" raw="); printfHex32(record.raw_calls);
        printf((char*)" mounted="); printfHex32(store.Mounted());
        printf((char*)" open="); printfHex32(store.OpenCount()); printf((char*)"\n");
        PathRequire(runtime.Reap() == 1 && frames.getStatistics().freeFrames == baseline &&
            store.OpenCount() == 0 && files.Statistics().reclaimFailures == 0, "Reap frame and file restoration");
        printf((char*)"PATH REAP iteration="); printfHex32(iteration);
        printf((char*)" free="); printfHex32(frames.getStatistics().freeFrames);
        printf((char*)" expected="); printfHex32(baseline);
        printf((char*)" open="); printfHex32(store.OpenCount()); printf((char*)"\n");
    }
    PathFinish(true);
}
