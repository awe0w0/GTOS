#define NativeProcessSmoke ContentSharedMemoryUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include "../apps/native_content_build/gtos-platform/probe/memory_record.h"
#include <process/native_shared_memory.h>

namespace {
    uint8_t image[65536];
    void FinishShared(bool pass) __attribute__((noreturn));
    void FinishShared(bool pass) {
        printf(pass ? (char*)"CONTENT SHARED MEMORY PASS\n" : (char*)"CONTENT SHARED MEMORY FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void RequireShared(bool value, const char* reason) {
        if (value) return;
        printf((char*)"CONTENT SHARED MEMORY REJECT ");
        printf((char*)reason);
        printf((char*)"\n");
        FinishShared(false);
    }
}

extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    GlobalDescriptorTable gdt;
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    RequireShared(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frames");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end,
        (const MultibootInfo*)multiboot, 0, 0};
    RequireShared(paging.prepareIdentity(frames, config), "identity paging");
    NativeSharedMemory shared(frames);
    NativeRuntime runtime;
    RequireShared(runtime.AttachSharedMemory(shared) && runtime.PrepareStacks(paging, frames), "endpoint and stacks");
    RequireShared(paging.enable() && paging.sealForSharedProcessors(), "sealed paging template");
    RequireShared(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2), "native runtime");
    const uint32_t baseline = frames.getStatistics().freeFrames;
    Task ring0(&gdt, Ring0Task);
    RequireShared(tasks.AddTask(&ring0), "kernel peer");
    const auto* boot = static_cast<const MultibootInfo*>(multiboot);
    RequireShared((boot->flags & 8) && boot->moduleCount == 2, "two supplied ELF modules");
    const auto* modules = reinterpret_cast<const MultibootModule*>(boot->modules);
    interrupts.Activate();
    for (uint32_t mode = 0; mode < 2; ++mode) {
        const auto& module = modules[mode];
        RequireShared(module.end > module.start && module.end - module.start <= sizeof(image), "bounded ELF");
        const uint32_t bytes = module.end - module.start;
        for (uint32_t i = 0; i < bytes; ++i) image[i] = ((const uint8_t*)module.start)[i];
        uint32_t id = 0;
        RequireShared(runtime.CreateElf(image, bytes, id), "ELF admission");
        const NativeStatus status = WaitStopped(runtime, tasks, id);
        gtos_perfetto_probe::Record record = {};
        RequireShared(runtime.ReadMemory(id, gtos_perfetto_probe::RecordAddress, &record, sizeof(record)) &&
            record.version == 1 && record.mode == mode, "consumer record");
        RequireShared(status.observedCs == 0x23 && status.observedCr3 == status.directory &&
            status.directory != paging.getStatistics().directoryAddress, "CPL3 isolation");
        RequireShared(runtime.Reap() == 1 && frames.getStatistics().freeFrames == baseline,
            "Reap released tables and all backing frames");
        printf((char*)"CONTENT SHARED MEMORY RECORD mode="); printfHex32(mode);
        printf((char*)" checks="); printfHex32(record.checks);
        printf((char*)" failures="); printfHex32(record.failures);
        printf((char*)" exit="); printfHex32(status.exitCode);
        printf((char*)" vector="); printfHex32(status.faultVector);
        printf((char*)" error="); printfHex32(status.faultError);
        printf((char*)" free="); printfHex32(frames.getStatistics().freeFrames);
        printf((char*)" expected="); printfHex32(baseline);
        printf((char*)"\n");
        if (mode == 0) {
            RequireShared(record.stage == 2 && record.checks == 24 && !record.failures &&
                !status.exitCode && !status.faultVector, "alias semantics");
        } else {
            RequireShared(record.stage == 1 && record.checks == 2 && !record.failures &&
                record.fault_address >= ProcessAddressSpace::DynamicBase &&
                status.exitCode == 0x8000000EU && status.faultVector == 14 &&
                (status.faultError & 7) == 7 && status.faultAddress == record.fault_address,
                "write to read-only alias faulted");
        }
    }
    FinishShared(true);
}
