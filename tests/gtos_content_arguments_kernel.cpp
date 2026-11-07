#define NativeProcessSmoke ContentArgumentsUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke

namespace {
    uint8_t image[65536];
}

extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    const auto* boot = static_cast<const MultibootInfo*>(multiboot);
    Require((boot->flags & 4) && (boot->flags & 8) && boot->moduleCount == 1,
            "one CRT ELF and one explicit scene");
    const auto* scene = reinterpret_cast<const char*>(boot->commandLine);
    const char valid[] = "GTOS_CONTENT_ARG_COUNT=3\0"
                         "GTOS_CONTENT_ARG_01=--single-process\0"
                         "GTOS_CONTENT_ARG_02=file:///fixtures/page with spaces.html\0"
                         "GTOS_CONTENT_ARG_03=\0";
    const char missing[] = "GTOS_CONTENT_ARG_COUNT=2\0"
                           "GTOS_CONTENT_ARG_01=--single-process\0";
    const char overflow[] = "GTOS_CONTENT_ARG_COUNT=17\0";
    const char malformed[] = "GTOS_CONTENT_ARG_COUNT=1x\0";
    const char empty[] = "GTOS_CONTENT_ARG_COUNT=0\0";
    const char maximum[] = "GTOS_CONTENT_ARG_COUNT=16\0"
                           "GTOS_CONTENT_ARG_01=a\0GTOS_CONTENT_ARG_02=a\0"
                           "GTOS_CONTENT_ARG_03=a\0GTOS_CONTENT_ARG_04=a\0"
                           "GTOS_CONTENT_ARG_05=a\0GTOS_CONTENT_ARG_06=a\0"
                           "GTOS_CONTENT_ARG_07=a\0GTOS_CONTENT_ARG_08=a\0"
                           "GTOS_CONTENT_ARG_09=nine\0GTOS_CONTENT_ARG_10=ten\0"
                           "GTOS_CONTENT_ARG_11=a\0GTOS_CONTENT_ARG_12=a\0"
                           "GTOS_CONTENT_ARG_13=a\0GTOS_CONTENT_ARG_14=a\0"
                           "GTOS_CONTENT_ARG_15=a\0GTOS_CONTENT_ARG_16=sixteen\0";
    NativeEnvironment environment;
    uint32_t expected = 64;
    if (Contains(scene, "case=valid")) {
        Require(environment.Assign(valid, sizeof(valid) - 1), "valid launch environment");
        expected = 73;
    } else if (Contains(scene, "case=empty")) {
        Require(environment.Assign(empty, sizeof(empty) - 1), "empty launch arguments");
        expected = 37;
    } else if (Contains(scene, "case=default")) {
        expected = 37;
    } else if (Contains(scene, "case=maximum")) {
        Require(environment.Assign(maximum, sizeof(maximum) - 1), "maximum launch arguments");
        expected = 74;
    } else if (Contains(scene, "case=missing")) {
        Require(environment.Assign(missing, sizeof(missing) - 1), "missing argument fixture");
    } else if (Contains(scene, "case=overflow")) {
        Require(environment.Assign(overflow, sizeof(overflow) - 1), "argument limit fixture");
    } else {
        Require(Contains(scene, "case=malformed") &&
                environment.Assign(malformed, sizeof(malformed) - 1), "invalid decimal fixture");
    }
    const auto& module = *reinterpret_cast<const MultibootModule*>(boot->modules);
    Require(module.end > module.start && module.end - module.start <= sizeof(image), "bounded CRT ELF");
    const uint32_t bytes = module.end - module.start;
    for (uint32_t i = 0; i < bytes; ++i) image[i] = ((const uint8_t*)module.start)[i];

    GlobalDescriptorTable gdt;
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    Require(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frames");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end, boot, 0, 0};
    Require(paging.prepareIdentity(frames, config), "identity paging");
    NativeRuntime runtime;
    Require(runtime.PrepareStacks(paging, frames), "guarded stacks");
    Require(paging.enable() && paging.sealForSharedProcessors(), "sealed paging template");
    Require(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2, true), "native CRT runtime");
    const uint32_t baseline = frames.getStatistics().freeFrames;
    Task ring0(&gdt, Ring0Task);
    Require(tasks.AddTask(&ring0), "kernel peer");
    interrupts.Activate();

    uint32_t id = 0;
    Require(runtime.CreateElf(image, bytes, id, environment), "CRT ELF admission");
    const NativeStatus status = WaitStopped(runtime, tasks, id);
    Require(status.observedCs == 0x23 && status.observedCr3 == status.directory &&
            status.directory != paging.getStatistics().directoryAddress && !status.faultVector,
            "CRT CPL3 isolation");
    printf((char*)"CONTENT ARGS EXIT "); printfHex32(status.exitCode);
    printf((char*)" EXPECTED "); printfHex32(expected); printf((char*)"\n");
    Require(status.exitCode == expected, "CRT actual launch result");
    Require(runtime.Reap() == 1 && frames.getStatistics().freeFrames == baseline, "exact CRT Reap");
    printf((char*)"CONTENT ARGS REAP "); printfHex32(baseline); printf((char*)"\n");
    printf((char*)"CONTENT ARGS PASS\n");
    Finish(true);
}
