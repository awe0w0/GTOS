#define NativeProcessSmoke ContentLoopbackUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include "../apps/native_content_build/gtos-platform/probe/memory_record.h"
#include <process/native_loopback.h>

namespace {
    uint8_t image[65536];
}

extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    GlobalDescriptorTable gdt;
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    Require(frames.initialize(multiboot, magic, (uint32_t)&kernel_start,
        (uint32_t)&kernel_end), "loopback frames");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end,
        (const MultibootInfo*)multiboot, 0, 0};
    Require(paging.prepareIdentity(frames, config), "loopback paging");
    NativeLoopback loopback;
    NativeRuntime runtime;
    Require(runtime.AttachLoopback(loopback) && !runtime.AttachLoopback(loopback) &&
        runtime.PrepareStacks(paging, frames), "loopback endpoint");
    Require(paging.enable() && paging.sealForSharedProcessors(), "loopback sealed paging");
    Require(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2),
        "loopback runtime");
    const uint32_t baseline = frames.getStatistics().freeFrames;
    Task ring0(&gdt, Ring0Task);
    Require(tasks.AddTask(&ring0), "loopback kernel peer");
    const auto* boot = static_cast<const MultibootInfo*>(multiboot);
    Require((boot->flags & 8) && boot->moduleCount == 2, "two loopback consumers");
    const auto* modules = reinterpret_cast<const MultibootModule*>(boot->modules);
    auto admit = [&](unsigned mode) {
        const auto& module = modules[mode];
        const uint32_t bytes = module.end - module.start;
        Require(module.end > module.start && bytes <= sizeof(image), "loopback bounded ELF");
        for (uint32_t i = 0; i < bytes; ++i) image[i] = ((const uint8_t*)module.start)[i];
        uint32_t id = 0;
        Require(runtime.CreateElf(image, bytes, id), "loopback admission");
        return id;
    };
    const uint32_t server = admit(0);
    interrupts.Activate();
    gtos_perfetto_probe::Record serverRecord = {};
    const auto start = tasks.Ticks();
    do {
        Require(runtime.ReadMemory(server, gtos_perfetto_probe::RecordAddress,
            &serverRecord, sizeof(serverRecord)), "loopback listener state");
        if (serverRecord.stage == 1) break;
        WaitTicks(tasks, 1);
    } while (tasks.Ticks() - start < 80);
    Require(serverRecord.stage == 1, "loopback listener ready");
    const uint32_t client = admit(1);
    const NativeStatus serverStatus = WaitStopped(runtime, tasks, server);
    Require(runtime.ReadMemory(server, gtos_perfetto_probe::RecordAddress,
        &serverRecord, sizeof(serverRecord)) && serverRecord.version == 1 &&
        serverRecord.mode == 0 && serverRecord.stage == 2 && serverRecord.checks == 5 && !serverRecord.failures &&
        !serverStatus.exitCode && !serverStatus.faultVector && serverStatus.observedCs == 0x23,
        "loopback server CPL3 and transferred payload");
    Require(runtime.Reap() == 1, "loopback server cleanup");
    const NativeStatus clientStatus = WaitStopped(runtime, tasks, client);
    gtos_perfetto_probe::Record clientRecord = {};
    Require(runtime.ReadMemory(client, gtos_perfetto_probe::RecordAddress, &clientRecord,
        sizeof(clientRecord)) && clientRecord.version == 1 && clientRecord.mode == 1 && clientRecord.stage == 2 &&
        clientRecord.checks == 9 && !clientRecord.failures &&
        !clientStatus.exitCode && !clientStatus.faultVector && clientStatus.observedCs == 0x23 &&
        clientStatus.directory != serverStatus.directory, "loopback client CPL3 and service ownership");
    const uint32_t reaped = runtime.Reap();
    Require(reaped == 1 && frames.getStatistics().freeFrames == baseline,
        "loopback exact frames after both reaps");
    printf((char*)"CONTENT LOOPBACK OWNER PASS free=");
    printfHex32(frames.getStatistics().freeFrames);
    printf((char*)" expected=");
    printfHex32(baseline);
    printf((char*)"\n");
    Finish(true);
}
