#define NativeProcessSmoke ContentExitUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke

namespace {
    uint8_t exitElf[65536];
    void ExitFinish(bool pass) __attribute__((noreturn));
    void ExitFinish(bool pass) {
        printf(pass ? (char*)"CONTENT EXIT GUEST PASS\n" : (char*)"CONTENT EXIT GUEST FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void ExitRequire(bool value, const char* reason) {
        if (!value) {
            printf((char*)"CONTENT EXIT FAILED ");
            printf((char*)reason);
            printf((char*)"\n");
            ExitFinish(false);
        }
    }
}

extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    GlobalDescriptorTable gdt;
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    ExitRequire(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frames");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end,
        (const MultibootInfo*)multiboot, 0, 0};
    ExitRequire(paging.prepareIdentity(frames, config), "identity");
    NativeRuntime runtime;
    ExitRequire(runtime.PrepareStacks(paging, frames), "guarded stacks");
    ExitRequire(paging.enable() && paging.sealForSharedProcessors(), "sealed paging template");
    ExitRequire(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2), "native activation");
    const uint32_t baseline = frames.getStatistics().freeFrames;
    Task ring0(&gdt, Ring0Task);
    ExitRequire(tasks.AddTask(&ring0), "kernel peer");
    const auto* boot = static_cast<const MultibootInfo*>(multiboot);
    ExitRequire((boot->flags & 8) && boot->moduleCount == 1, "one supplied ELF");
    const auto* module = reinterpret_cast<const MultibootModule*>(boot->modules);
    ExitRequire(module->end > module->start && module->end - module->start <= sizeof(exitElf), "bounded ELF");
    const uint32_t bytes = module->end - module->start;
    for (uint32_t i = 0; i < bytes; ++i) exitElf[i] = ((const uint8_t*)module->start)[i];

    interrupts.Activate();
    uint32_t id;
    ExitRequire(runtime.CreateElf(exitElf, bytes, id), "ELF admission");
    const NativeStatus status = WaitStopped(runtime, tasks, id);
    unsigned stage = 0;
    ExitRequire(runtime.ReadMemory(id, 0x40020000U, &stage, sizeof(stage)) && stage == 2, "consumer stage");
    ExitRequire(status.observedCs == 0x23 && status.observedCr3 == status.directory &&
        status.directory != paging.getStatistics().directoryAddress, "CPL3 isolation");
    printf((char*)"CONTENT EXIT STATUS code="); printfHex32(status.exitCode);
    printf((char*)" vector="); printfHex32(status.faultVector);
    printf((char*)" calls="); printfHex32(status.systemCalls); printf((char*)"\n");
    ExitRequire(status.exitCode == 0x12345678U && status.faultVector == 0 &&
        status.systemCalls == 2, "exact exit status");
    ExitRequire(runtime.Reap() == 1 && frames.getStatistics().freeFrames == baseline, "exact Reap frames");
    printf((char*)"CONTENT EXIT REAP free="); printfHex32(frames.getStatistics().freeFrames);
    printf((char*)" expected="); printfHex32(baseline); printf((char*)"\n");
    ExitRequire(ring0Progress && tasks.BootTicks(), "kernel peer progress");
    ExitFinish(true);
}
