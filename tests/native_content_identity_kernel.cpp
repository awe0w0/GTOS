#define NativeProcessSmoke ContentIdentityUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include "../apps/native_content_build/gtos-platform/probe/memory_record.h"

namespace {
    uint8_t image[65536];
    void FinishIdentity(bool pass) __attribute__((noreturn));
    void FinishIdentity(bool pass) {
        printf(pass ? (char*)"CONTENT BASE IDENTITY PASS\n" : (char*)"CONTENT BASE IDENTITY FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void Need(bool value, const char* reason) {
        if (!value) {
            printf((char*)"CONTENT BASE IDENTITY REJECT ");
            printf((char*)reason);
            printf((char*)"\n");
            FinishIdentity(false);
        }
    }
}

extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    GlobalDescriptorTable gdt;
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    Need(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frames");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end,
        (const MultibootInfo*)multiboot, 0, 0};
    Need(paging.prepareIdentity(frames, config), "identity paging");
    NativeRuntime runtime;
    Need(runtime.PrepareStacks(paging, frames), "guarded stacks");
    Need(paging.enable() && paging.sealForSharedProcessors(), "sealed paging template");
    Need(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2, true), "bounded native threads");
    const uint32_t baseline = frames.getStatistics().freeFrames;
    Task ring0(&gdt, Ring0Task);
    Need(tasks.AddTask(&ring0), "kernel peer");
    const auto* boot = static_cast<const MultibootInfo*>(multiboot);
    Need((boot->flags & 8) && boot->moduleCount == 1, "one supplied ELF");
    const auto& module = *reinterpret_cast<const MultibootModule*>(boot->modules);
    Need(module.end > module.start && module.end - module.start <= sizeof(image), "bounded ELF");
    const uint32_t bytes = module.end - module.start;
    for (uint32_t i = 0; i < bytes; ++i) image[i] = ((const uint8_t*)module.start)[i];
    interrupts.Activate();
    uint32_t id = 0;
    Need(runtime.CreateElf(image, bytes, id), "ELF admission");
    const NativeStatus status = WaitStopped(runtime, tasks, id);
    gtos_perfetto_probe::Record record = {};
    Need(runtime.ReadMemory(id, gtos_perfetto_probe::RecordAddress, &record, sizeof(record)) &&
        record.version == 1 && record.mode == 0 && record.stage == 2, "completed consumer record");
    Need(status.observedCs == 0x23 && status.observedCr3 == status.directory &&
        status.directory != paging.getStatistics().directoryAddress && !status.faultVector,
        "CPL3 isolation");
    printf((char*)"CONTENT BASE IDENTITY RECORD process="); printfHex32(record.base);
    printf((char*)" worker="); printfHex32(record.fault_address);
    printf((char*)" checks="); printfHex32(record.checks);
    printf((char*)" failures="); printfHex32(record.failures);
    printf((char*)" exit="); printfHex32(status.exitCode); printf((char*)"\n");
    Need(record.base == id && record.fault_address > id && record.checks == 23 &&
        !record.failures && status.exitCode == 0, "exact process and worker identity");
    Need(runtime.Reap() == 1 && frames.getStatistics().freeFrames == baseline, "exact Reap frames");
    printf((char*)"CONTENT BASE IDENTITY REAP free="); printfHex32(frames.getStatistics().freeFrames);
    printf((char*)" expected="); printfHex32(baseline); printf((char*)"\n");
    FinishIdentity(true);
}
