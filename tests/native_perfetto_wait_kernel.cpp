#define NativeProcessSmoke PerfettoWaitUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include "../apps/native_content_build/gtos-platform/probe/wait_record.h"
#include <process/clock_abi.h>
#include <process/native_events.h>

namespace {
    uint8_t waitElf[65536];
    void WaitFinish(bool pass) __attribute__((noreturn));
    void WaitFinish(bool pass) {
        printf(pass ? (char*)"PERFETTO WAIT GUEST PASS\n" : (char*)"PERFETTO WAIT GUEST FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void WaitRequire(bool value, const char* reason) {
        if (!value) { printf((char*)"PWAIT FAILED "); printf((char*)reason); printf((char*)"\n"); WaitFinish(false); }
    }
}
extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    printf((char*)"PERFETTO WAIT GUEST BOOT\n");
    GlobalDescriptorTable gdt;
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    WaitRequire(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frames");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end,
        (const MultibootInfo*)multiboot, 0, 0};
    WaitRequire(paging.prepareIdentity(frames, config), "identity");
    NativeEvents events;
    NativeRuntime runtime;
    WaitRequire(runtime.AttachEvents(events), "explicit native event endpoint");
    WaitRequire(runtime.PrepareStacks(paging, frames), "guarded stacks");
    WaitRequire(paging.enable() && paging.sealForSharedProcessors(), "frozen paging template");
    WaitRequire(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2), "native SSE2 activation");
    const uint32_t baseline = frames.getStatistics().freeFrames;
    Task ring0(&gdt, Ring0Task);
    WaitRequire(tasks.AddTask(&ring0), "independent kernel task");
    const MultibootInfo* boot = (const MultibootInfo*)multiboot;
    WaitRequire((boot->flags & 8) && boot->moduleCount == 3, "three current-source modules");
    const MultibootModule* modules = (const MultibootModule*)boot->modules;
    asm volatile("outb %0,$0x43" : : "a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR & 255)));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR >> 8)));
    interrupts.Activate();
    for (uint32_t mode = 0; mode < 3; ++mode) {
        const auto before = events.Statistics();
        const auto& module = modules[mode];
        WaitRequire(module.end > module.start && module.end - module.start <= sizeof(waitElf), "bounded ELF");
        const uint32_t bytes = module.end - module.start;
        for (uint32_t i = 0; i < bytes; ++i) waitElf[i] = ((const uint8_t*)module.start)[i];
        Elf32LoadPlan plan;
        WaitRequire(ValidateElf32(waitElf, bytes, plan), "native ELF plan");
        uint32_t id;
        WaitRequire(runtime.CreateElf(waitElf, bytes, id), "actual ELF admission");
        const NativeStatus status = WaitStopped(runtime, tasks, id);
        gtos_perfetto_wait_probe::Record record = {};
        WaitRequire(runtime.ReadMemory(id, gtos_perfetto_wait_probe::RecordAddress, &record, sizeof(record)), "consumer record");
        const auto current = events.Statistics();
        WaitRequire(record.version == 1 && record.mode == mode && !record.failures &&
            record.stage == (mode ? 1U : 2U) && record.checks == (mode ? 1U : 14U) &&
            record.waits == (mode ? 1U : 8U), "consumer result");
        WaitRequire(status.observedCs == 0x23 && status.observedCr3 == status.directory &&
            status.directory != paging.getStatistics().directoryAddress, "CPL3 native execution");
        WaitRequire(status.exitCode == (mode ? 0x80000006U : 0U) && status.faultVector == (mode ? 6U : 0U), "exact exit/fault policy");
        WaitRequire(mode ? (status.statistics.sleeps == 0 && status.statistics.yields == 0) :
            (status.statistics.sleeps >= 3 && status.statistics.yields > 0), "actual sleep and yield scheduling");
        WaitRequire(current.handles == (mode ? 1U : 0U) && current.objects == current.handles &&
            current.created - before.created == (mode ? 1U : 2U) &&
            current.closed - before.closed == (mode ? 0U : 2U) &&
            current.signals - before.signals == (mode ? 0U : 3U), "event ownership/control");
        printf((char*)"PWAIT CASE mode="); printfHex32(mode);
        printf((char*)" checks="); printfHex32(record.checks);
        printf((char*)" cs="); printfHex32(status.observedCs);
        printf((char*)" calls="); printfHex32(status.systemCalls);
        printf((char*)" waits="); printfHex32(record.waits);
        printf((char*)" sleeps="); printfHex32(status.statistics.sleeps);
        printf((char*)" yields="); printfHex32(status.statistics.yields);
        printf((char*)" exit="); printfHex32(status.exitCode);
        printf((char*)" vector="); printfHex32(status.faultVector);
        printf((char*)" handles="); printfHex32(current.handles);
        printf((char*)" created="); printfHex32(current.created - before.created);
        printf((char*)" closed="); printfHex32(current.closed - before.closed);
        printf((char*)" signals="); printfHex32(current.signals - before.signals); printf((char*)"\n");
        WaitRequire(runtime.Reap() == 1 && frames.getStatistics().freeFrames == baseline, "exact Reap frames");
        const auto reaped = events.Statistics();
        WaitRequire(!reaped.handles && !reaped.objects && reaped.reclaims - before.reclaims == 1 &&
            reaped.released - before.released == (mode ? 1U : 0U), "exact Reap events");
        printf((char*)"PWAIT REAP mode="); printfHex32(mode);
        printf((char*)" free="); printfHex32(frames.getStatistics().freeFrames);
        printf((char*)" expected="); printfHex32(baseline);
        printf((char*)" handles="); printfHex32(reaped.handles);
        printf((char*)" released="); printfHex32(reaped.released - before.released); printf((char*)"\n");
    }
    WaitRequire(ring0Progress && tasks.BootTicks(), "surviving kernel scheduling");
    WaitFinish(true);
}
