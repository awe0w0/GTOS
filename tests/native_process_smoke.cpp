#include <process/native_runtime.h>
#include <process/elf32.h>
#include <hardwarecommunication/interrupts.h>
#include <syscalls.h>
using namespace gtos;
using namespace gtos::memory;
using namespace gtos::process;
using namespace gtos::hardwarecommunication;
extern "C" uint8_t kernel_start, kernel_end, kernel_readonly_start, kernel_readonly_end;
extern "C" uint8_t native_user_start, native_user_end;
void printf(char* text) { while (*text) { asm volatile("outb %0,$0xe9" : : "a"(*text)); ++text; } }
void printfHex32(uint32_t value) {
    for (int shift = 28; shift >= 0; shift -= 4) {
        char c = "0123456789ABCDEF"[(value >> shift) & 15];
        asm volatile("outb %0,$0xe9" : : "a"(c));
    }
}
void printfHex(uint8_t value) { printfHex32(value); }
void operator delete(void*) {}
namespace {
    volatile uint32_t ring0Progress;
    uint32_t protectedSentinel = 0xC0DEC0DE;
    uint8_t userData[4096];
    uint8_t elfCopy[32768];
    uint32_t pressureFrames[65536];
    bool haveSysenter, haveSyscall;
    uint32_t originalEferLow, originalEferHigh;
    void UnexpectedFastEntry() { printf((char*)"FAILED inherited fast entry reached\n"); for (;;) asm volatile("cli; hlt"); }
    void SeedFastEntry() {
        uint32_t a, b, c, d;
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
        haveSysenter = (d & (1U << 11)) != 0;
        if (haveSysenter) {
            asm volatile("wrmsr" : : "c"(0x174), "a"(0x10), "d"(0));
            asm volatile("wrmsr" : : "c"(0x175), "a"(NativeRuntime::KernelStackArena + 4096 * 5), "d"(0));
            asm volatile("wrmsr" : : "c"(0x176), "a"((uint32_t)&UnexpectedFastEntry), "d"(0));
        }
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000U), "c"(0));
        if (a >= 0x80000001U) {
            asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000001U), "c"(0));
            haveSyscall = (d & (1U << 11)) != 0;
            if (haveSyscall) {
                const bool haveNxe = (d & (1U << 20)) != 0;
                asm volatile("rdmsr" : "=a"(originalEferLow), "=d"(originalEferHigh) : "c"(0xC0000080U));
                // Seed an unrelated supported bit too: preserve it while
                // clearing SCE. NXE has no page execute effect in non-PAE mode.
                if (haveNxe) originalEferLow |= (1U << 11);
                asm volatile("wrmsr" : : "c"(0xC0000080U), "a"(originalEferLow | 1U), "d"(originalEferHigh));
            }
        }
    }
    void Put32(uint8_t* p, uint32_t value) {
        for (uint32_t i = 0; i < 4; ++i) p[i] = value >> (i * 8);
    }
    uint32_t Get32(const uint8_t* p) {
        return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    void Finish(bool pass) __attribute__((noreturn));
    void Finish(bool pass) {
        printf(pass ? (char*)"NATIVE PROCESS SMOKE PASS\n" : (char*)"NATIVE PROCESS SMOKE FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void Require(bool value, const char* why) {
        if (!value) { printf((char*)"FAILED "); printf((char*)why); printf((char*)"\n"); Finish(false); }
    }
    bool Contains(const char* text, const char* word) {
        if (!text) return false;
        for (; *text; ++text) {
            uint32_t i = 0; while (word[i] && text[i] == word[i]) ++i;
            if (!word[i]) return true;
        }
        return false;
    }
    void Ring0Task() { for (;;) ++ring0Progress; }
    void WaitTicks(TaskManager& tasks, uint32_t ticks) {
        uint32_t begin = tasks.Ticks();
        while ((uint32_t)(tasks.Ticks() - begin) < ticks) asm volatile("sti; hlt" : : : "memory");
    }
    uint32_t Create(NativeRuntime& runtime, uint32_t sentinel, uint32_t mode) {
        for (uint32_t i = 0; i < sizeof(userData); ++i) userData[i] = 0;
        ((uint32_t*)userData)[0] = sentinel;
        ((uint32_t*)userData)[1] = mode;
        ((uint32_t*)userData)[2] = (uint32_t)&protectedSentinel;
        userData[4095] = 'X';
        NativeImage image = {&native_user_start, (uint32_t)(&native_user_end - &native_user_start),
            0, userData, sizeof(userData)};
        uint32_t id;
        Require(runtime.Create(image, id), "create user fixture");
        return id;
    }
    NativeStatus WaitStopped(NativeRuntime& runtime, TaskManager& tasks, uint32_t id) {
        uint32_t begin = tasks.Ticks(); NativeStatus status = {};
        do {
            Require(runtime.Status(id, status), "status present");
            if (!status.live) return status;
            WaitTicks(tasks, 1);
        } while ((uint32_t)(tasks.Ticks() - begin) < 500);
        Require(false, "native task stops within deadline"); return status;
    }
}
extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    printf((char*)"NATIVE PROCESS SMOKE BOOT\n");
    const MultibootInfo* bootInfo = (const MultibootInfo*)multiboot;
    const bool osfxsr = (bootInfo->flags & 4)
        && Contains((const char*)bootInfo->commandLine, "case=osfxsr");
    const bool requireSce = (bootInfo->flags & 4)
        && Contains((const char*)bootInfo->commandLine, "case=sce");
    GlobalDescriptorTable gdt;
    Require(gdt.CodeSegmentSelector() == 0x10 && gdt.DataSegmentSelector() == 0x18,
        "kernel selectors preserved");
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    Require(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frames initialize");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end,
        (const MultibootInfo*)multiboot, 0, 0};
    Require(paging.prepareIdentity(frames, config), "identity prepare");
    NativeRuntime runtime;
    PhysicalMemoryManager wrongFrames;
    Require(wrongFrames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end),
        "independent allocator for ownership rejection");
    const uint32_t correctFree = frames.getStatistics().freeFrames;
    const uint32_t wrongFree = wrongFrames.getStatistics().freeFrames;
    Require(!runtime.PrepareStacks(paging, wrongFrames)
        && frames.getStatistics().freeFrames == correctFree
        && wrongFrames.getStatistics().freeFrames == wrongFree,
        "stack preparation rejects mismatched allocator before mutation");
    Require(runtime.PrepareStacks(paging, frames), "retained guard stacks before sharing");
    Require(paging.enable() && paging.sealForSharedProcessors(), "enable and seal template");
    if (osfxsr) {
        uint32_t cr4; asm volatile("mov %%cr4,%0" : "=r"(cr4));
        cr4 |= 0x200; asm volatile("mov %0,%%cr4" : : "r"(cr4) : "memory");
    }
    SeedFastEntry();
    Require(!requireSce || haveSyscall, "SCE feature case actually advertises SYSCALL"); // Deliberately unsafe inherited state must be closed by Activate.
    Require(runtime.Activate(tasks, gdt, paging, frames), "activate CPL3 runtime");
    if (haveSysenter) for (uint32_t msr = 0x174; msr <= 0x176; ++msr) {
        uint32_t low, high; asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
        Require(!low && !high, "inherited SYSENTER CS/ESP/EIP closed");
    }
    if (haveSyscall) {
        uint32_t low, high; asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0xC0000080U));
        Require(low == (originalEferLow & ~1U) && high == originalEferHigh,
            "inherited EFER SCE closed preserving other bits");
    }
    printf((char*)"FAST ENTRY CLOSED SEP="); printfHex32(haveSysenter);
    printf((char*)" SCE="); printfHex32(haveSyscall); printf((char*)"\n");
    uint16_t tr; asm volatile("str %0" : "=r"(tr)); Require(tr == 0x30, "TSS loaded");
    PagingMapping mapping;
    for (uint32_t i = 0; i <= NativeRuntime::MaximumProcesses; ++i)
        Require(!paging.query(NativeRuntime::KernelStackArena + i * 5 * 4096, mapping), "kernel stack guards absent");
    uint32_t baseline = frames.getStatistics().freeFrames;
    Task ring0(&gdt, Ring0Task); Require(tasks.AddTask(&ring0), "kernel peer task");
    uint32_t first = Create(runtime, 0x11223344, 0), second = Create(runtime, 0x55667788, 0);
    NativeStatus firstState = {}, secondState = {};
    Require(runtime.Status(first, firstState) && runtime.Status(second, secondState)
        && firstState.directory != secondState.directory, "distinct process CR3s");
    // 100 Hz PIT; timer IRQs remain the only clock-accounting source.
    asm volatile("outb %0,$0x43" : : "a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(11932 & 255)));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(11932 >> 8)));
    interrupts.Activate();
    WaitTicks(tasks, 30);
    uint32_t one[4], two[4];
    Require(runtime.ReadMemory(first, NativeRuntime::DataAddress, one, sizeof(one))
        && runtime.ReadMemory(second, NativeRuntime::DataAddress, two, sizeof(two)), "read private data");
    Require(one[0] == 0x11223344 && two[0] == 0x55667788 && one[3] && two[3],
        "same VA distinct data and CPU-bound progress");
    runtime.Status(first, firstState); runtime.Status(second, secondState);
    Require((firstState.observedCs & 3) == 3 && (secondState.observedCs & 3) == 3
        && firstState.observedCr3 == firstState.directory && secondState.observedCr3 == secondState.directory,
        "kernel records CPL3 and actual private CR3");
    Require(firstState.statistics.runTicks >= 3 && secondState.statistics.runTicks >= 3
        && tasks.BootTicks() >= 3 && ring0Progress, "user/ring0/boot timer preemption");
    printf((char*)"NATIVE CPL3 DISTINCT CR3 SAME VA PREEMPTION PASS\n");
    Require(runtime.RequestExit(second, 7), "terminate inactive peer");
    Require(runtime.Reap() == 1, "reap only from surviving boot context");
    uint32_t survivorBaseline = frames.getStatistics().freeFrames;
    const uint32_t expectedVectors[] = {0, 14, 13, 13, 13, 6, 0, 14, 14, 7, 13};
    for (uint32_t mode = 1; mode <= 10; ++mode) {
        uint32_t id = Create(runtime, 0x33445566, mode);
        NativeStatus victim = WaitStopped(runtime, tasks, id);
        Require(!victim.live && victim.exitCode == (0x80000000U | expectedVectors[mode])
            && victim.faultVector == expectedVectors[mode], "user fault isolation vector");
        if (mode == 1) Require(victim.faultAddress == (uint32_t)&protectedSentinel
            && (victim.faultError & 7) == 7, "supervisor write page fault bits");
        if (mode == 7) Require(victim.faultAddress == NativeRuntime::CodeAddress
            && (victim.faultError & 7) == 7, "user text write blocked");
        if (mode == 8) Require(victim.faultAddress == NativeRuntime::UserStackBottom - 4
            && (victim.faultError & 7) == 6, "user stack guard page fault bits");
        Require(protectedSentinel == 0xC0DEC0DE, "kernel data unchanged");
        Require(runtime.Reap() == 1 && frames.getStatistics().freeFrames == survivorBaseline,
            "fault reap exact frame restoration");
        Require(runtime.ReadMemory(first, NativeRuntime::DataAddress, one, sizeof(one))
            && one[0] == 0x11223344 && one[3], "survivor persists after peer fault");
    }
    printf((char*)"NATIVE USER FAULT CONTAINMENT PASS\n");
    for (uint32_t mode = 12; mode <= 27; ++mode) {
        uint32_t id = Create(runtime, 0xAA55AA55, mode);
        NativeStatus result = WaitStopped(runtime, tasks, id);
        const bool normal = mode == 16 || mode == 17 || mode == 18 || mode == 20 || mode == 21;
        if (normal) Require(result.exitCode == 0, "NT/TF/segment/ESP adversarial syscall resumes safely");
        else {
            const uint32_t vector = mode == 12 ? (osfxsr ? 7 : 6)
                : mode <= 15 ? 7 : mode == 24 ? 10 : mode == 25 ? (haveSysenter ? 13 : 6)
                : mode == 26 ? 6 : mode == 27 ? (haveSyscall ? 13 : 6) : 13;
            Require(result.exitCode == (0x80000000U | vector) && result.faultVector == vector,
                "FP/MMX/SSE/save/wait/SS/far-return/IO adversarial fault isolated");
        }
        Require(runtime.Reap() == 1 && frames.getStatistics().freeFrames == survivorBaseline,
            "adversarial probe exact reap restoration");
    }
    printf((char*)"NATIVE FP SIMD NT TF SELECTOR ESP PROTECTION PASS\n");
    for (uint32_t iteration = 0; iteration < 8; ++iteration) {
        uint32_t beforeWritten = runtime.Statistics().writtenBytes;
        uint32_t id = Create(runtime, 0xAABBCCDD, 11);
        NativeStatus result = WaitStopped(runtime, tasks, id);
        Require(result.exitCode == 0 && result.faultVector == 0, "bounded syscall and segment/DF tests");
        Require(result.statistics.runTicks > 0, "timer interrupted null segments and DF");
        Require(runtime.Statistics().writtenBytes == beforeWritten + 5,
            "invalid syscall buffers produce no partial output");
        Require(runtime.Reap() == 1 && frames.getStatistics().freeFrames == survivorBaseline,
            "repeated normal exit/reap exact frame restoration");
    }
    printf((char*)"NATIVE VALIDATED SYSCALL SEGMENTS DF REAP PASS\n");
    // Fill all remaining slots, then fail closed with unchanged ownership.
    uint32_t extra[3]; for (uint32_t i = 0; i < 3; ++i) extra[i] = Create(runtime, i + 1, 0);
    NativeImage image = {&native_user_start, (uint32_t)(&native_user_end - &native_user_start), 0, 0, 0};
    uint32_t failed = 0, fullFree = frames.getStatistics().freeFrames;
    Require(!runtime.Create(image, failed) && !failed && frames.getStatistics().freeFrames == fullFree,
        "process capacity fails without leaking frames");
    for (uint32_t i = 0; i < 3; ++i) Require(runtime.RequestExit(extra[i], 0), "stop extra process");
    Require(runtime.RequestExit(first, 0) && runtime.Reap() == 4, "reap all processes");
    Require(frames.getStatistics().freeFrames == baseline, "all private frame ownership restored");
    WaitTicks(tasks, 10);
    Require(ring0Progress && tasks.BootTicks(), "legacy kernel task and boot survive final reap");
    // Actual external ELF is supplied by GRUB, assembled/linked independently.
    const MultibootInfo* boot = (const MultibootInfo*)multiboot;
    Require((boot->flags & 8) && boot->moduleCount == 1, "ELF GRUB module present");
    const MultibootModule* module = (const MultibootModule*)boot->modules;
    const uint8_t* elf = (const uint8_t*)module->start;
    const uint32_t elfBytes = module->end - module->start;
    Require(elfBytes <= sizeof(elfCopy), "bounded ELF fixture size");
    Elf32LoadPlan plan; Require(ValidateElf32(elf, elfBytes, plan), "GNUas ld ELF validates");
    uint32_t elfFirst, elfSecond;
    Require(runtime.CreateElf(elf, elfBytes, elfFirst) && runtime.CreateElf(elf, elfBytes, elfSecond),
        "load two independently mapped external ELF programs");
    NativeStatus elfState = WaitStopped(runtime, tasks, elfFirst);
    NativeStatus elfPeer = WaitStopped(runtime, tasks, elfSecond);
    Require(elfState.exitCode == 0 && elfPeer.exitCode == 0 && elfState.directory != elfPeer.directory
        && (elfState.observedCs & 3) == 3 && (elfPeer.observedCs & 3) == 3
        && elfState.statistics.runTicks && elfPeer.statistics.runTicks, "ELF native execution preempted and isolated");
    uint32_t elfData[4];
    Require(runtime.ReadMemory(elfFirst, 0x40020000, elfData, sizeof(elfData))
        && elfData[0] == 0xDECABEEF && elfData[2], "ELF initialized data and live writes");
    uint32_t bssValue;
    Require(runtime.ReadMemory(elfFirst, 0x40020010, &bssValue, 4)
        && bssValue == 0x1EE7C0DE, "ELF BSS zero and private mutation");
    Require(runtime.Reap() == 2 && frames.getStatistics().freeFrames == baseline, "ELF deferred reap baseline");
    for (uint32_t i = 0; i < elfBytes; ++i) elfCopy[i] = elf[i];
    uint32_t badId;
    elfCopy[0] = 0;
    Require(!runtime.CreateElf(elfCopy, elfBytes, badId) && !badId
        && frames.getStatistics().freeFrames == baseline, "malformed ELF rollback before allocation");
    elfCopy[0] = elf[0];
    Require(!runtime.CreateElf(elfCopy, 51, badId) && !badId, "truncated ELF rejected");
    Require(!runtime.CreateElf((const uint8_t*)0, elfBytes, badId)
        && !runtime.CreateElf((const uint8_t*)0xFFFFFFFFU, 52, badId)
        && !runtime.CreateElf((const uint8_t*)0xFEE00000U, 52, badId), "ELF source pointers checked before dereference");
    const uint32_t phOffset = Get32(elfCopy + 28);
    const uint32_t dataHeader = phOffset + 32; // Fixture has exactly code + data loads.
    Require(plan.segmentCount == 2, "two fixture segments");
    const uint32_t dataVa = Get32(elfCopy + dataHeader + 8);
    for (uint32_t address = NativeRuntime::UserStackBottom - 4096;
         address <= NativeRuntime::UserStackTop; address += 4096) {
        Put32(elfCopy + dataHeader + 8, address);
        Require(!runtime.CreateElf(elfCopy, elfBytes, badId) && !badId
            && frames.getStatistics().freeFrames == baseline, "ELF stack or guard collision rejected");
    }
    Put32(elfCopy + dataHeader + 8, dataVa);
    const uint32_t memorySize = Get32(elfCopy + dataHeader + 20);
    Put32(elfCopy + dataHeader + 20, 255 * 4096);
    Require(!runtime.CreateElf(elfCopy, elfBytes, badId) && !badId
        && frames.getStatistics().freeFrames == baseline, "ELF page cap includes runtime stack");
    Put32(elfCopy + dataHeader + 20, memorySize);
    // Final permissions, not merely parser flags: user text writes really fault.
    const uint32_t dataOffset = Get32(elfCopy + dataHeader + 4);
    Put32(elfCopy + dataOffset + 4, 1);
    Require(runtime.CreateElf(elfCopy, elfBytes, badId), "ELF readonly-code fault fixture");
    NativeStatus elfFault = WaitStopped(runtime, tasks, badId);
    Require(elfFault.faultVector == 14 && elfFault.faultAddress == plan.entry
        && (elfFault.faultError & 7) == 7, "ELF code sealed hardware readonly");
    Require(runtime.Reap() == 1 && frames.getStatistics().freeFrames == baseline, "ELF fault reap baseline");
    Put32(elfCopy + dataOffset + 4, 0);
    // Exhaust the real allocator, then release each prefix of the number of
    // frames one ELF creation needs. Every failed stage must roll back exactly.
    uint32_t pressureCount = 0, frame;
    asm volatile("cli" : : : "memory");
    // Batch acquisition avoids making the allocator's intentional first-fit
    // scan quadratic in the guest RAM size; ownership is still real frames.
    while (pressureCount <= 65536 - 256 && frames.allocateContiguous(256, frame))
        for (uint32_t i = 0; i < 256; ++i) pressureFrames[pressureCount++] = frame + i * 4096;
    while (pressureCount < 65536 && frames.allocate(frame)) pressureFrames[pressureCount++] = frame;
    Require(frames.getStatistics().freeFrames == 0, "real guest allocator exhausted");
    bool allocationSucceeded = false;
    for (uint32_t available = 0; available < 20; ++available) {
        uint32_t freeBefore = frames.getStatistics().freeFrames;
        uint32_t tasksBefore = tasks.TaskCount();
        bool loaded = runtime.CreateElf(elf, elfBytes, badId);
        if (loaded) {
            Require(runtime.RequestExit(badId, 0) && runtime.Reap() == 1,
                "successful low-memory ELF cancel before first dispatch");
            Require(frames.getStatistics().freeFrames == freeBefore, "successful low-memory ELF cleanup");
            allocationSucceeded = true; break;
        }
        Require(!badId && tasks.TaskCount() == (int)tasksBefore
            && frames.getStatistics().freeFrames == freeBefore, "ELF every allocation failure rollback");
        Require(pressureCount && frames.free(pressureFrames[--pressureCount]), "release one frame for next allocation stage");
    }
    Require(allocationSucceeded, "ELF allocator recovery after all failure stages");
    while (pressureCount) Require(frames.free(pressureFrames[--pressureCount]), "release allocator pressure");
    Require(frames.getStatistics().freeFrames == baseline, "ELF failure matrix restores total baseline");
    asm volatile("sti" : : : "memory");
    WaitTicks(tasks, 10);
    printf((char*)"NATIVE ELF32 LOAD BSS ISOLATION PERMISSIONS ROLLBACK PASS\n");
    Finish(true);
}
