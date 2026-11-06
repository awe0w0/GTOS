#define NativeProcessSmoke PerfettoCpuUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include "../apps/native_content_build/gtos-platform/probe/cpu_time_record.h"
#include <process/clock_abi.h>

namespace {
    uint8_t cpuElf[65536];
    void CpuFinish(bool pass) __attribute__((noreturn));
    void CpuFinish(bool pass) {
        printf(pass ? (char*)"PERFETTO CPU GUEST PASS\n" : (char*)"PERFETTO CPU GUEST FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void CpuRequire(bool value, const char* reason) {
        if (!value) {
            printf((char*)"PCPU FAILED "); printf((char*)reason); printf((char*)"\n");
            CpuFinish(false);
        }
    }
}

extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    printf((char*)"PERFETTO CPU GUEST BOOT\n");
    GlobalDescriptorTable gdt;
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    CpuRequire(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frames");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end,
        (const MultibootInfo*)multiboot, 0, 0};
    CpuRequire(paging.prepareIdentity(frames, config), "identity");
    NativeRuntime runtime;
    CpuRequire(runtime.PrepareStacks(paging, frames), "guarded kernel stacks");
    CpuRequire(paging.enable() && paging.sealForSharedProcessors(), "frozen paging template");
    CpuRequire(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2), "native SSE2 activation");
    const uint32_t baseline = frames.getStatistics().freeFrames;
    Task ring0(&gdt, Ring0Task);
    CpuRequire(tasks.AddTask(&ring0), "independent kernel task");
    const MultibootInfo* boot = (const MultibootInfo*)multiboot;
    CpuRequire((boot->flags & 8) && boot->moduleCount == 3, "three current-source modules");
    const MultibootModule* modules = (const MultibootModule*)boot->modules;
    asm volatile("outb %0,$0x43" : : "a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR & 255)));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR >> 8)));
    interrupts.Activate();
    for (uint32_t mode = 0; mode < 3; ++mode) {
        const MultibootModule& module = modules[mode];
        CpuRequire(module.end > module.start && module.end - module.start <= sizeof(cpuElf), "bounded ELF");
        const uint32_t bytes = module.end - module.start;
        for (uint32_t index = 0; index < bytes; ++index) cpuElf[index] = ((const uint8_t*)module.start)[index];
        Elf32LoadPlan plan;
        CpuRequire(ValidateElf32(cpuElf, bytes, plan), "native ELF plan");
        uint32_t id;
        CpuRequire(runtime.CreateElf(cpuElf, bytes, id), "actual ELF admission");
        const NativeStatus status = WaitStopped(runtime, tasks, id);
        gtos_perfetto_cpu_probe::Record record = {};
        CpuRequire(runtime.ReadMemory(id, gtos_perfetto_cpu_probe::RecordAddress, &record, sizeof(record)), "consumer record");
        CpuRequire(record.version == 1 && record.mode == mode && record.stage == 2 &&
            record.checks == 12 && !record.failures && status.exitCode == 0 && !status.faultVector,
            "consumer result");
        CpuRequire(status.observedCs == 0x23 && status.observedCr3 == status.directory &&
            status.directory != paging.getStatistics().directoryAddress &&
            status.systemCalls == record.cpu_queries + record.wall_queries + 3, "CPL3 exact native queries");
        CpuRequire(record.end_us >= record.start_us && record.end_us - record.start_us >= (3 + mode) * 10000 &&
            record.process_us >= record.end_us && record.wall_us >= record.end_us - record.start_us &&
            record.wall_us <= 2000000 && record.cpu_queries >= 6 && record.wall_queries >= 3,
            "delivered CPU accounting and wall bounds");
        printf((char*)"PCPU CASE mode="); printfHex32(mode);
        printf((char*)" checks="); printfHex32(record.checks);
        printf((char*)" cs="); printfHex32(status.observedCs);
        printf((char*)" cpu="); printfHex32(record.cpu_queries);
        printf((char*)" wall="); printfHex32(record.wall_queries);
        printf((char*)" calls="); printfHex32(status.systemCalls);
        printf((char*)" start="); printfHex32(record.start_us);
        printf((char*)" end="); printfHex32(record.end_us);
        printf((char*)" process="); printfHex32(record.process_us);
        printf((char*)" elapsed="); printfHex32(record.wall_us); printf((char*)"\n");
        CpuRequire(runtime.Reap() == 1 && frames.getStatistics().freeFrames == baseline, "exact Reap frame restoration");
        printf((char*)"PCPU REAP mode="); printfHex32(mode);
        printf((char*)" free="); printfHex32(frames.getStatistics().freeFrames);
        printf((char*)" expected="); printfHex32(baseline); printf((char*)"\n");
    }
    CpuRequire(ring0Progress && tasks.BootTicks(), "surviving kernel scheduling");
    CpuFinish(true);
}
