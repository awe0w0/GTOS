#define NativeProcessSmoke ContentBoringSslUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include "../apps/native_content_build/gtos-platform/probe/memory_record.h"

namespace {
    uint8_t image[65536];
    void ContentBoringSslFinish(bool pass) __attribute__((noreturn));
    void ContentBoringSslFinish(bool pass) {
        printf(pass ? (char*)"CONTENT BORINGSSL GUEST PASS\n" : (char*)"CONTENT BORINGSSL GUEST FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void ContentBoringSslRequire(bool value, const char* reason) {
        if (!value) {
            printf((char*)"CONTENT BORINGSSL FAILED ");
            printf((char*)reason);
            printf((char*)"\n");
            ContentBoringSslFinish(false);
        }
    }
}

extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    GlobalDescriptorTable gdt;
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    ContentBoringSslRequire(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frames");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end,
        (const MultibootInfo*)multiboot, 0, 0};
    ContentBoringSslRequire(paging.prepareIdentity(frames, config), "identity");
    NativeRuntime runtime;
    ContentBoringSslRequire(runtime.PrepareStacks(paging, frames), "guarded stacks");
    ContentBoringSslRequire(paging.enable() && paging.sealForSharedProcessors(), "sealed paging template");
    ContentBoringSslRequire(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2), "native activation");
    const uint32_t baseline = frames.getStatistics().freeFrames;
    Task ring0(&gdt, Ring0Task);
    ContentBoringSslRequire(tasks.AddTask(&ring0), "kernel peer");
    const auto* boot = static_cast<const MultibootInfo*>(multiboot);
    ContentBoringSslRequire((boot->flags & 8) && boot->moduleCount == 3, "three supplied ELFs");
    const auto* modules = reinterpret_cast<const MultibootModule*>(boot->modules);

    asm volatile("outb %0,$0x43" : : "a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(11932 & 255)));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(11932 >> 8)));
    interrupts.Activate();
    for (uint32_t mode = 0; mode < 3; ++mode) {
        const auto& module = modules[mode];
        ContentBoringSslRequire(module.end > module.start && module.end - module.start <= sizeof(image), "bounded ELF");
        const uint32_t bytes = module.end - module.start;
        for (uint32_t i = 0; i < bytes; ++i) image[i] = ((const uint8_t*)module.start)[i];
        uint32_t id;
        ContentBoringSslRequire(runtime.CreateElf(image, bytes, id), "ELF admission");
        const NativeStatus status = WaitStopped(runtime, tasks, id);
        gtos_perfetto_probe::Record record = {};
        ContentBoringSslRequire(runtime.ReadMemory(id, gtos_perfetto_probe::RecordAddress, &record,
                    sizeof(record)) && record.version == 1 && record.mode == mode,
                "consumer record");
        ContentBoringSslRequire(status.observedCs == 0x23 && status.observedCr3 == status.directory &&
            status.directory != paging.getStatistics().directoryAddress, "CPL3 isolation");
        printf((char*)"CONTENT BORINGSSL STATUS mode="); printfHex32(mode);
        printf((char*)" code="); printfHex32(status.exitCode);
        printf((char*)" vector="); printfHex32(status.faultVector);
        printf((char*)" calls="); printfHex32(status.systemCalls);
        printf((char*)" checks="); printfHex32(record.checks);
        printf((char*)" failures="); printfHex32(record.failures);
        printf((char*)" fault="); printfHex32(status.faultAddress); printf((char*)"\n");
        if (mode == 0) {
            ContentBoringSslRequire(record.stage == 2 && record.checks == 28 && !record.failures &&
                    status.exitCode == 0 && status.faultVector == 0 && status.systemCalls >= 9,
                    "MD5, native synchronization and hardware entropy");
        } else {
            ContentBoringSslRequire(record.stage == 1 && !record.checks && !record.failures &&
                    status.exitCode == 0x48000021 && status.faultVector == 0,
                    mode == 1 ? "reject recursive lock upgrade" : "terminate failed assertion");
        }
        ContentBoringSslRequire(runtime.Reap() == 1 && frames.getStatistics().freeFrames == baseline, "exact Reap frames");
        printf((char*)"CONTENT BORINGSSL REAP mode="); printfHex32(mode);
        printf((char*)" free="); printfHex32(frames.getStatistics().freeFrames);
        printf((char*)" expected="); printfHex32(baseline); printf((char*)"\n");
    }
    WaitTicks(tasks, 10);
    ContentBoringSslRequire(ring0Progress && tasks.BootTicks(), "kernel peer progress");
    ContentBoringSslFinish(true);
}
