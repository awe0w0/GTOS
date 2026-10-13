#include "fixture-helpers.h"
#include "record.h"
#include <memory/criticalsection.h>
#include <process/native_files.h>
#include <storage/filestore.h>
#include <drivers/ata.h>
using namespace gtos::storage;
using namespace gtos::drivers;
namespace {
unsigned checks;
void Finish(bool pass) __attribute__((noreturn));
void Finish(bool pass) {
    printf(pass ? (char*)"NATIVE THREAD GUEST PASS\n" : (char*)"NATIVE THREAD GUEST FAIL\n");
    asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
    for (;;) asm volatile("cli; hlt");
}
void Need(bool pass, const char* why) {
    ++checks;
    if (!pass) { asm volatile("cli" : : : "memory"); printf((char*)"FAILED THREAD "); printf((char*)why); printf((char*)"\n"); Finish(false); }
}
void Value(const char* name, unsigned value) { printf((char*)name); printfHex32(value); }
ThreadRecord Record(NativeRuntime& runtime, unsigned id) {
    ThreadRecord r = {}; Need(runtime.ReadMemory(id, THREAD_RECORD_VA, &r, sizeof(r)), "shared record checked copy"); return r;
}
NativeStatus Wait(NativeRuntime& runtime, TaskManager& tasks, unsigned id, bool retained) {
    const unsigned begin = tasks.Ticks();
    for (;;) {
        NativeStatus s = {}; Need(runtime.Status(id, s), "current root status");
        ThreadRecord r = Record(runtime, id);
        if (r.error) { Value(" error_line=", r.error); printf((char*)"\n"); }
        Need(!r.error, "user assertions");
        if (retained ? r.stage == 10 : !s.live) return s;
        Need(s.live && tasks.Ticks()-begin < 5000, "bounded scheduler progress"); WaitTicks(tasks, 1);
    }
}
}
asm(".section .text.native_thread_unused_peer,\"ax\"\n.balign 16\n"
    ".global native_user_start,native_user_end\nnative_user_start:\n"
    "incl 0x4000200c\njmp native_user_start\nnative_user_end:\n.text\n");
extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    printf((char*)"NATIVE THREAD BOOT\n");
    GlobalDescriptorTable gdt; TaskManager tasks; InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80); PhysicalMemoryManager frames;
    Need(frames.initialize(multiboot, magic, (unsigned)&kernel_start, (unsigned)&kernel_end), "physical allocator");
    KernelPaging paging;
    PagingConfig config = {(unsigned)&kernel_start, (unsigned)&kernel_end,
        (unsigned)&kernel_readonly_start, (unsigned)&kernel_readonly_end, (const MultibootInfo*)multiboot, 0, 0};
    AdvancedTechnologyAttachment disk(0x1f0, true); FileStore store;
    Need(paging.prepareIdentity(frames, config) && disk.Identify() && disk.SectorCount()==2064, "private ATA geometry");
    Need(store.Format(&disk, 8, 2048)==0 && store.Mount(&disk, 8, 2048)==0 && store.MakeDirectory("/native")==0, "private real filesystem");
    NativeFiles files(store); NativeRuntime runtime;
    Need(runtime.AttachFiles(files) && runtime.PrepareStacks(paging, frames) && paging.enable()
        && paging.sealForSharedProcessors(), "native context stacks");
    Need(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2, true) && runtime.FpEnabled(), "opt-in bounded native threads");
    const unsigned initial = frames.getStatistics().freeFrames, kernelCr3 = paging.getStatistics().directoryAddress;
    const MultibootInfo* boot = (const MultibootInfo*)multiboot;
    Need((boot->flags & 8) && boot->moduleCount == 1, "one admitted user ELF");
    const MultibootModule& module = *((const MultibootModule*)boot->modules);
    Elf32LoadPlan plan = {}; Need(ValidateElf32((const uint8_t*)module.start, module.end-module.start, plan), "actual user ELF plan");
    unsigned recordOffset = 0;
    for (unsigned i=0; i<plan.segmentCount; ++i)
        if (plan.segments[i].virtualAddress == THREAD_RECORD_VA) recordOffset = plan.segments[i].fileOffset;
    Need(recordOffset && recordOffset <= module.end-module.start-sizeof(ThreadRecord), "mode record inside module");
    Need(Get32((const uint8_t*)module.start+recordOffset)==1, "actual fixture version");
    Task ring0(&gdt, Ring0Task); Need(tasks.AddTask(&ring0), "ring0 continuation");
    asm volatile("outb %0,$0x43" : : "a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR & 255)));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR >> 8)));
    interrupts.Activate(); WaitTicks(tasks, 8);
    for (unsigned mode=0; mode<4; ++mode) {
        unsigned foreignId = 0, foreignWorker = 0;
        if (mode == 3) {
            ThreadRecord peer = {}; peer.version = 1; peer.mode = 4;
            for (unsigned i=0; i<sizeof(peer); ++i) ((uint8_t*)module.start)[recordOffset+i] = ((const uint8_t*)&peer)[i];
            Need(runtime.CreateElf((const uint8_t*)module.start, module.end-module.start, foreignId), "independent foreign process");
            Wait(runtime, tasks, foreignId, true);
            const ThreadRecord observed = Record(runtime, foreignId); foreignWorker = observed.worker[0].id;
            NativeStatus member = {}; Need(runtime.Status(foreignWorker, member) && member.live, "real foreign live worker");
        }
        ThreadRecord fresh = {}; fresh.version = 1; fresh.foreignId = foreignWorker;
        for (unsigned i=0; i<sizeof(fresh); ++i) ((uint8_t*)module.start)[recordOffset+i] = ((const uint8_t*)&fresh)[i];
        Put32((uint8_t*)module.start+recordOffset+4, mode);
        const NativeFpStatistics fpBefore = runtime.FpStatistics(); const NativeFileStatistics fileBefore = files.Statistics();
        const unsigned ringBefore = ring0Progress, bootBefore = tasks.BootTicks(); unsigned id = 0;
        Need(runtime.CreateElf((const uint8_t*)module.start, module.end-module.start, id), "fresh native root");
        if (mode == 1) { Wait(runtime, tasks, id, true); Need(runtime.RequestExit(id, 73), "cancel actual same-process workers"); }
        NativeStatus status = Wait(runtime, tasks, id, false); ThreadRecord r = Record(runtime, id);
        Need(!status.live && !status.reaped && !r.error && status.observedCs==0x23
            && status.observedCr3==status.directory && status.directory!=kernelCr3, "real stopped CPL3 root");
        const unsigned workers = mode==0 ? 11 : mode==3 ? 1 : 2;
        Need(r.workerCount==workers, "actual worker creation count");
        Need(mode==0 ? r.stage==20 && r.loops==8 && r.finalMxcsr==0x1f80 && !status.exitCode
            : mode==3 ? r.stage==20 && !status.exitCode : r.stage==10 && status.exitCode==(mode==1 ? 73U : 0x8000000eU), "normal/cancel/fault group outcome");
        unsigned outstanding = 1;
        for (unsigned i=0; i<(mode==0 ? 3U : mode==3 ? 1U : 2U); ++i) {
            NativeStatus worker = {}; Need(runtime.Status(r.worker[i].id, worker), "actual worker status");
            Need(!worker.live && worker.directory==status.directory && worker.observedCs==0x23
                && worker.observedCr3==status.directory && r.worker[i].initialMxcsr==0x1f80 && r.worker[i].loops, "independent CPL3 worker with shared CR3");
            if (mode==1 || mode==2) {
                Need(!worker.reaped && worker.exitCode==status.exitCode, "group cancellation reaches each member"); ++outstanding;
                if (mode==2 && i==0) Need(worker.faultVector==14 && worker.faultError==7 && worker.faultAddress==0x100000, "actual worker page fault");
            } else Need(worker.reaped && worker.exitCode==90+i && r.worker[i].finalMxcsr==0x3f80+i*0x2000, "worker joined once with private FP");
        }
        Need(store.OpenCount()==(mode ? 1U : 0U), "worker exit keeps process FILE ownership");
        if (mode==3) Need(runtime.RequestExit(foreignId, 0), "stop independent foreign process");
        const unsigned reap = runtime.Reap();
        Need(reap==outstanding+(mode==3 ? 2U : 0U) && frames.getStatistics().freeFrames==initial
            && !store.OpenCount() && tasks.TaskCount()==1 && runtime.Reap()==0, "all shared frames/resources recovered once");
        const NativeFpStatistics fpAfter = runtime.FpStatistics(); const NativeFileStatistics fileAfter = files.Statistics();
        // Foreign initialization precedes fpBefore; its two deferred invalidations belong to this reap.
        Need(fpAfter.initialized-fpBefore.initialized==workers+1
            && fpAfter.invalidated-fpBefore.invalidated==workers+1+(mode==3 ? 2U : 0U)
            && !fpAfter.invariantFailures, "each context FP owner recovered exactly once");
        Need(fileAfter.released-fileBefore.released==(mode ? 1U : 0U)
            && !fileAfter.reclaimFailures, "actual FILE owner release");
        WaitTicks(tasks, 8); Need(ring0Progress!=ringBefore && tasks.BootTicks()>bootBefore, "kernel and ring0 still live");
        printf((char*)"THREAD CASE"); Value(" mode=", mode); Value(" checks=", r.checks); Value(" workers=", workers);
        Value(" reaps=", reap); Value(" free=", frames.getStatistics().freeFrames); Value(" baseline=", initial);
        Value(" fp_init=", fpAfter.initialized); Value(" fp_invalid=", fpAfter.invalidated); printf((char*)"\n");
    }
    Need(store.Unmount()==0, "filesystem unmount"); Value("THREAD KERNEL CHECKS=", checks); printf((char*)"\n"); Finish(true);
}
