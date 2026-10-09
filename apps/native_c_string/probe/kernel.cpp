#define NativeProcessSmoke StringUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include <memory/criticalsection.h>
#include "record.h"
#include <process/clock_abi.h>
namespace {
    uint8_t retained[12288];
    void StringFinish(bool pass) __attribute__((noreturn));
    void StringFinish(bool pass) {
        printf(pass ? (char*)"NATIVE STRING DIAGNOSTIC PASS\n" : (char*)"NATIVE STRING DIAGNOSTIC FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void StringRequire(bool pass, const char* why) {
        if (!pass) {
            asm volatile("cli" : : : "memory");
            printf((char*)"FAILED STRING "); printf((char*)why); printf((char*)"\n"); StringFinish(false);
        }
    }
    StringRecord Read(NativeRuntime& runtime, uint32_t id) {
        StringRecord result = {};
        StringRequire(runtime.ReadMemory(id, GTOS_STRING_RECORD_VA, &result, sizeof(result)), "retained record");
        return result;
    }
    NativeStatus StringWaitStopped(NativeRuntime& runtime, TaskManager& tasks, uint32_t id) {
        const uint32_t begin = tasks.Ticks(); NativeStatus status = {};
        do {
            StringRequire(runtime.Status(id, status), "actual task status");
            if (!status.live) return status;
            WaitTicks(tasks, 1);
        } while (tasks.Ticks() - begin < 5000);
        StringRequire(false, "bounded actual string matrix"); return status;
    }
    uint32_t Cost(const Elf32LoadPlan& plan) {
        uint32_t seen[32] = {}, tables = 0;
        for (uint32_t i = 0; i < plan.segmentCount; ++i) {
            const Elf32LoadSegment& s = plan.segments[i];
            if (!s.memorySize) continue;
            for (uint32_t di = s.virtualAddress >> 22; di <= (s.virtualAddress + s.memorySize - 1) >> 22; ++di) {
                const uint32_t bit = 1U << (di & 31);
                if (!(seen[di >> 5] & bit)) { seen[di >> 5] |= bit; ++tables; }
            }
        }
        if (!(seen[(NativeRuntime::UserStackBottom >> 22) >> 5] & (1U << ((NativeRuntime::UserStackBottom >> 22) & 31)))) ++tables;
        return plan.pageCount + 2 + tables + 1;
    }
}
asm(".section .text.native_string_peer,\"ax\"\n.balign 16\n"
    ".global native_user_start,native_user_end\nnative_user_start:\n"
    "incl 0x4000200c\njmp native_user_start\nnative_user_end:\n.text\n");
extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    printf((char*)"NATIVE STRING DIAGNOSTIC BOOT\n");
    GlobalDescriptorTable gdt; TaskManager tasks; InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80); PhysicalMemoryManager frames;
    StringRequire(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "initialize frames");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end, (uint32_t)&kernel_readonly_start,
        (uint32_t)&kernel_readonly_end, (const MultibootInfo*)multiboot, 0, 0};
    StringRequire(paging.prepareIdentity(frames, config), "identity paging");
    NativeRuntime runtime;
    StringRequire(runtime.PrepareStacks(paging, frames) && paging.enable() && paging.sealForSharedProcessors(), "guarded sealed paging");
    StringRequire(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2)
        && runtime.FpEnabled() && runtime.FpError() == NativeFpOk, "activate production FP");
    const uint32_t kernelCr3 = paging.getStatistics().directoryAddress;
    const uint32_t initial = frames.getStatistics().freeFrames;
    const MultibootInfo* boot = (const MultibootInfo*)multiboot;
    StringRequire((boot->flags & 8) && boot->moduleCount == 4, "four actual printf ELF modes");
    const MultibootModule* modules = (const MultibootModule*)boot->modules;
    Task ring0(&gdt, Ring0Task); StringRequire(tasks.AddTask(&ring0), "ring0 peer");
    const uint32_t peer = Create(runtime, 0x11223344, 0);
    asm volatile("outb %0,$0x43" : : "a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR & 255)));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR >> 8)));
    interrupts.Activate(); WaitTicks(tasks, 8);
    const uint32_t survivor = frames.getStatistics().freeFrames;
    StringRequire(initial - survivor == 7, "raw peer exact cost");
    for (uint32_t iteration = 0; iteration < 5; ++iteration) {
        const uint32_t mode = iteration == 4 ? 0 : iteration;
        uint32_t peerBefore[4]; StringRequire(runtime.ReadMemory(peer, NativeRuntime::DataAddress, peerBefore, sizeof(peerBefore)), "peer snapshot");
        const uint32_t ringBefore = ring0Progress, bootBefore = tasks.BootTicks();
        const uint32_t bytes = modules[mode].end - modules[mode].start;
        Elf32LoadPlan plan; StringRequire(modules[mode].end > modules[mode].start
            && ValidateElf32((const uint8_t*)modules[mode].start, bytes, plan), "bounded production ELF admission");
        const uint32_t cost = Cost(plan); uint32_t id;
        const NativeFpStatistics beforeFp = runtime.FpStatistics();
        { InterruptGuard guard; StringRequire(runtime.CreateElf((const uint8_t*)modules[mode].start, bytes, id), "CreateElf"); }
        if (mode == 2) {
            const uint32_t start = tasks.Ticks();
            for (;;) {
                StringRecord record; NativeStatus status;
                { InterruptGuard guard; record = Read(runtime, id); StringRequire(runtime.Status(id, status), "waiting status"); }
                if (record.stage == 3 && record.repeats >= 100) break;
                StringRequire(status.live && tasks.Ticks() - start < 5000, "bounded actual repeat phase"); WaitTicks(tasks, 1);
            }
            StringRequire(runtime.RequestExit(id, 73), "external cancel");
        }
        const NativeStatus status = StringWaitStopped(runtime, tasks, id);
        const StringRecord record = Read(runtime, id);
        { InterruptGuard guard;
          printf((char*)"STRING RECORD words=");
          const uint32_t* words = (const uint32_t*)&record;
          for (uint32_t i = 0; i < sizeof(record) / 4; ++i) { printfHex32(words[i]); printf((char*)" "); }
          printf((char*)"\n"); }
        { InterruptGuard guard;
          printf((char*)"STRING CASE mode="); printfHex32(mode); printf((char*)" id="); printfHex32(id); printf((char*)" stage="); printfHex32(record.stage);
          printf((char*)" error="); printfHex32(record.error); printf((char*)" checks="); printfHex32(record.checks);
          printf((char*)" cases="); printfHex32(record.cases); printf((char*)" repeats="); printfHex32(record.repeats); printf((char*)" exit="); printfHex32(status.exitCode);
          printf((char*)" vector="); printfHex32(status.faultVector); printf((char*)" pf="); printfHex32(status.faultError);
          printf((char*)" cr2="); printfHex32(status.faultAddress); printf((char*)" cs="); printfHex32(status.observedCs);
          printf((char*)" cr3="); printfHex32(status.observedCr3); printf((char*)" kernel_cr3="); printfHex32(kernelCr3);
          printf((char*)" load_pages="); printfHex32(plan.pageCount); printf((char*)" cost="); printfHex32(cost); printf((char*)"\n"); }
        StringRequire(record.version == 1 && record.mode == mode && !record.error && record.checks
            && record.base == 0x80000000U && record.handle && record.cases == 99063 && record.suites == 8191,
            "actual default byte/string checks");
        for (uint32_t i = 0; i < 13; ++i) StringRequire(record.calls[i] != 0, "all actual providers called");
        StringRequire(status.observedCs == 0x23 && status.observedCr3 == status.directory && status.directory != kernelCr3
            && status.directory >= 4096, "real CPL3/private CR3");
        StringRequire(record.heap_base == 0x80005000U && record.heap_bytes == 65536
            && record.errno_va >= record.heap_base && record.errno_va <= record.heap_base + record.heap_bytes - 4
            && record.errno_value == 0x5533, "real private TLS heap");
        uint32_t savedErrno; StringRequire(runtime.ReadMemory(id, record.errno_va, &savedErrno, 4) && savedErrno == record.errno_value, "retained errno");
        StringRequire(record.stage == (mode == 0 ? 2U : 3U), "completed diagnostic stage");
        if (mode == 1 || mode == 3) StringRequire(status.faultVector == 14 && status.faultError == (mode == 1 ? 4U : 6U)
            && status.faultAddress == 0x80004000U && status.exitCode == 0x8000000EU, "actual read or write guard fault contained");
        else StringRequire(!status.faultVector && status.exitCode == (mode == 2 ? 73U : 0U), "normal exit or cancellation");
        StringRequire(runtime.ReadMemory(id, record.base + 4096, retained, sizeof(retained)), "all actual owned test pages retained");
        uint32_t forbidden = 0;
        StringRequire(!runtime.ReadMemory(id, record.base, &forbidden, 1)
            && !runtime.ReadMemory(id, record.base + 16384, &forbidden, 1), "both guard pages remain absent");
        StringRequire(survivor - frames.getStatistics().freeFrames == cost + 20 && !status.live && !status.reaped, "exact stopped cost");
        StringRequire(runtime.Reap() == 1 && frames.getStatistics().freeFrames == survivor, "exact victim Reap");
        const NativeFpStatistics afterFp = runtime.FpStatistics();
        StringRequire(afterFp.initialized == beforeFp.initialized + 1 && afterFp.invalidated == beforeFp.invalidated + 1
            && afterFp.saves > beforeFp.saves && afterFp.restores > beforeFp.restores && !afterFp.invariantFailures, "real FP lifecycle");
        WaitTicks(tasks, 8);
        uint32_t peerAfter[4]; NativeStatus peerStatus;
        StringRequire(runtime.ReadMemory(peer, NativeRuntime::DataAddress, peerAfter, sizeof(peerAfter))
            && runtime.Status(peer, peerStatus) && peerStatus.live && !peerStatus.systemCalls && peerAfter[3] != peerBefore[3]
            && peerAfter[0] == 0x11223344 && ring0Progress != ringBefore && tasks.BootTicks() > bootBefore, "peers continue");
        { InterruptGuard guard;
          printf((char*)"STRING REAP free="); printfHex32(survivor); printf((char*)" expected="); printfHex32(frames.getStatistics().freeFrames);
          printf((char*)" fp_initialized="); printfHex32(afterFp.initialized); printf((char*)" fp_invalidated="); printfHex32(afterFp.invalidated);
          printf((char*)" fp_saves="); printfHex32(afterFp.saves); printf((char*)" fp_restores="); printfHex32(afterFp.restores);
          printf((char*)" fp_failures="); printfHex32(afterFp.invariantFailures); printf((char*)"\n"); }
    }
    StringRequire(runtime.RequestExit(peer, 0) && runtime.Reap() == 1 && frames.getStatistics().freeFrames == initial, "allocator baseline");
    const NativeFpStatistics finalFp = runtime.FpStatistics();
    StringRequire(finalFp.initialized == 6 && finalFp.invalidated == 6 && !finalFp.invariantFailures, "all FP owners reclaimed");
    { InterruptGuard guard;
      printf((char*)"STRING FINAL free="); printfHex32(frames.getStatistics().freeFrames); printf((char*)" expected="); printfHex32(initial);
      printf((char*)" fp_initialized="); printfHex32(finalFp.initialized); printf((char*)" fp_invalidated="); printfHex32(finalFp.invalidated);
      printf((char*)" fp_failures="); printfHex32(finalFp.invariantFailures); printf((char*)"\n"); StringFinish(true); }
}
