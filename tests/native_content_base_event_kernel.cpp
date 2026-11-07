#define NativeProcessSmoke ContentBaseEventUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include "../apps/native_content_build/gtos-platform/probe/memory_record.h"
#include <process/native_events.h>

namespace {
    uint8_t image[65536];
    void FinishEvent(bool pass) __attribute__((noreturn));
    void FinishEvent(bool pass) {
        printf(pass ? (char*)"CONTENT BASE EVENT PASS\n" : (char*)"CONTENT BASE EVENT FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void RequireEvent(bool value, const char* reason) {
        if (value) return;
        printf((char*)"CONTENT BASE EVENT REJECT ");
        printf((char*)reason);
        printf((char*)"\n");
        FinishEvent(false);
    }
}

extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    GlobalDescriptorTable gdt;
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    RequireEvent(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frames");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end,
        (const MultibootInfo*)multiboot, 0, 0};
    RequireEvent(paging.prepareIdentity(frames, config), "identity paging");
    NativeEvents events;
    NativeRuntime runtime;
    RequireEvent(runtime.AttachEvents(events) && runtime.PrepareStacks(paging, frames), "endpoint and stacks");
    RequireEvent(paging.enable() && paging.sealForSharedProcessors(), "sealed paging template");
    RequireEvent(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2), "native runtime");
    const uint32_t baseline = frames.getStatistics().freeFrames;
    Task ring0(&gdt, Ring0Task);
    RequireEvent(tasks.AddTask(&ring0), "kernel peer");
    const auto* boot = static_cast<const MultibootInfo*>(multiboot);
    RequireEvent((boot->flags & 8) && boot->moduleCount == 1, "one supplied ELF");
    const auto& module = *reinterpret_cast<const MultibootModule*>(boot->modules);
    RequireEvent(module.end > module.start && module.end - module.start <= sizeof(image), "bounded ELF");
    const uint32_t bytes = module.end - module.start;
    for (uint32_t i = 0; i < bytes; ++i) image[i] = ((const uint8_t*)module.start)[i];
    interrupts.Activate();
    uint32_t id = 0;
    RequireEvent(runtime.CreateElf(image, bytes, id), "ELF admission");
    const NativeStatus status = WaitStopped(runtime, tasks, id);
    gtos_perfetto_probe::Record record = {};
    RequireEvent(runtime.ReadMemory(id, gtos_perfetto_probe::RecordAddress, &record, sizeof(record)) &&
        record.version == 1 && record.stage == 2, "completed consumer record");
    RequireEvent(status.observedCs == 0x23 && status.observedCr3 == status.directory &&
        status.directory != paging.getStatistics().directoryAddress && !status.faultVector, "CPL3 isolation");
    const auto count = events.Statistics();
    RequireEvent(count.created == 3 && count.closed == 3 && count.signals == 4 &&
        !count.handles && !count.objects, "native event ownership and control");
    RequireEvent(runtime.Reap() == 1 && frames.getStatistics().freeFrames == baseline, "exact Reap frames");
    printf((char*)"CONTENT BASE EVENT RECORD checks="); printfHex32(record.checks);
    printf((char*)" failures="); printfHex32(record.failures);
    printf((char*)" exit="); printfHex32(status.exitCode);
    printf((char*)" free="); printfHex32(frames.getStatistics().freeFrames);
    printf((char*)" expected="); printfHex32(baseline);
    printf((char*)"\n");
    RequireEvent(record.checks == 10 && !record.failures && !status.exitCode, "event semantics");
    FinishEvent(true);
}
