// Integer-only kernel fixture. FP observations are made by real CPL3 payloads.
#include <process/native_runtime.h>
#include <process/native_fp.h>
#include <hardwarecommunication/interrupts.h>
#include <syscalls.h>
using namespace gtos;
using namespace gtos::memory;
using namespace gtos::process;
using namespace gtos::hardwarecommunication;
extern "C" uint8_t kernel_start, kernel_end, kernel_readonly_start, kernel_readonly_end;
extern "C" uint8_t native_fp_user_start, native_fp_user_end;
extern "C" void native_fp_test_kernel_misuse();
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
    uint8_t userData[4096] __attribute__((aligned(16)));
    uint8_t observation[4096];
    uint8_t supervisorTarget[512] __attribute__((aligned(16)));
    uint32_t expectedCr0, expectedCr4;
    bool pointerLimitedDiagnostic, xmLimitedDiagnostic, mxcsrLimitedDiagnostic;
    const uint32_t ownedCr0 = 0x2E, ownedCr4 = 0x600;
    uint8_t In(uint16_t port) { uint8_t value; asm volatile("inb %1,%0" : "=a"(value) : "Nd"(port)); return value; }
    void Out(uint16_t port, uint8_t value) { asm volatile("outb %0,%1" : : "a"(value), "Nd"(port)); }
    void Finish(bool pass) __attribute__((noreturn));
    void Finish(bool pass) {
        printf(!pass ? (char*)"NATIVE FP SMOKE FAIL\n" : (pointerLimitedDiagnostic || xmLimitedDiagnostic || mxcsrLimitedDiagnostic)
            ? (char*)"NATIVE FP DIAGNOSTIC PASS (EMULATOR GAPS REMAIN)\n"
            : (char*)"NATIVE FP SMOKE PASS\n");
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
    uint16_t U16(const uint8_t* p) { return p[0] | ((uint16_t)p[1] << 8); }
    uint32_t U32(const uint8_t* p) { return U16(p) | ((uint32_t)U16(p + 2) << 16); }
    void Put16(uint32_t at, uint16_t value) { userData[at] = value; userData[at+1] = value >> 8; }
    void Put32(uint32_t at, uint32_t value) { Put16(at, value); Put16(at+2, value >> 16); }
    uint32_t Cr0() { uint32_t v; asm volatile("mov %%cr0,%0" : "=r"(v)); return v; }
    uint32_t Cr4() { uint32_t v; asm volatile("mov %%cr4,%0" : "=r"(v)); return v; }
    void CheckControls() {
        Require((Cr0() & ~ownedCr0) == (expectedCr0 & ~ownedCr0)
            && (Cr4() & ~ownedCr4) == (expectedCr4 & ~ownedCr4), "unrelated CR0/CR4 bits preserved");
        Require((Cr0() & ownedCr0) == 0x2A && (Cr4() & ownedCr4) == ownedCr4
            && !(Cr4() & (1U << 18)), "boot kernel TS guarded, legacy controls exact");
    }
    void Ring0Task() { for (;;) ++ring0Progress; }
    void WaitTicks(TaskManager& tasks, uint32_t ticks) {
        uint32_t begin = tasks.Ticks();
        while ((uint32_t)(tasks.Ticks() - begin) < ticks) asm volatile("sti; hlt" : : : "memory");
    }
    class InputIrq : public InterruptHandler {
    public:
        volatile uint32_t total, user;
        InputIrq(InterruptsManager& manager, uint8_t vector) : InterruptHandler(&manager, vector), total(0), user(0) {}
        uint32_t HandlerInterrupt(uint32_t esp) {
            (void)In(0x60); ++total;
            if ((((CPUState*)esp)->cs & 3) == 3) ++user;
            Require((Cr0() & ownedCr0) == 0x2A, "input IRQ executes with guarded kernel FP");
            return esp;
        }
    };
    void Ps2Ready() { for (uint32_t n = 0; n < 100000 && (In(0x64) & 2); ++n) {} }
    void Ps2Command(uint8_t byte) { Ps2Ready(); Out(0x64, byte); }
    void Ps2Data(uint8_t byte) { Ps2Ready(); Out(0x60, byte); }
    uint8_t Ps2Read() {
        for (uint32_t n = 0; n < 100000 && !(In(0x64) & 1); ++n) {}
        return In(0x60);
    }
    void InitInput() {
        for (uint32_t n = 0; n < 100000 && (In(0x64) & 1); ++n) (void)In(0x60);
        Ps2Command(0xAE); Ps2Command(0xA8); Ps2Command(0x20);
        uint8_t config = (Ps2Read() | 3) & ~0x30;
        Ps2Command(0x60); Ps2Data(config);
        Ps2Data(0xF4); (void)Ps2Read();
        Ps2Command(0xD4); Ps2Data(0xF4); (void)Ps2Read();
    }
    uint32_t Create(NativeRuntime& runtime, uint32_t mode, uint32_t seed) {
        for (uint32_t i = 0; i < sizeof(userData); ++i) userData[i] = 0;
        Put32(0, mode); Put32(4, seed); Put32(16, (uint32_t)supervisorTarget);
        Put32(48, pointerLimitedDiagnostic ? 1 : 0);
        for (uint32_t i = 0; i < 32; ++i) Put32(1696 + i * 4, seed * 0x1020304U + i * 0x01010101U);
        for (uint32_t i = 0; i < 16; ++i) Put32(1824 + i * 4, seed * 0x5060708U + i * 0x02020202U);
        for (uint32_t i = 0; i < 8; ++i) {
            Put32(1888 + i * 16, seed * 17 + i);
            Put32(1892 + i * 16, 0x80000000U | (seed << 16) | (i << 24));
            Put16(1896 + i * 16, 0x3FFF);
        }
        Put16(2016, (seed & 1) ? 0x0B7F : 0x067F);
        Put32(2020, (seed & 1) ? 0x5FA1 : 0x3F84);
        NativeImage image = {&native_fp_user_start, (uint32_t)(&native_fp_user_end - &native_fp_user_start),
            0, userData, sizeof(userData)};
        uint32_t id;
        Require(runtime.Create(image, id) && id, "admit initialized FP user");
        return id;
    }
    void Read(NativeRuntime& runtime, uint32_t id) {
        Require(runtime.ReadMemory(id, NativeRuntime::DataAddress, observation, sizeof(observation)), "inspect user-owned observations");
    }
    void CheckFresh(NativeRuntime& runtime, uint32_t id) {
        Read(runtime, id);
        const uint8_t* fx = observation + 64; const uint8_t* env = observation + 576;
        Require(U16(fx) == 0x37F && !U16(fx+2) && !fx[4] && !(U16(fx+6) & 0x7FF)
            && !U32(fx+8) && !U16(fx+12) && !U32(fx+16) && !U16(fx+20)
            && U32(fx+24) == 0x1F80, "first-instruction canonical FX controls/pointers/MXCSR");
        for (uint32_t r = 0; r < 8; ++r)
            for (uint32_t b = 0; b < 10; ++b)
                Require(!fx[32 + r*16 + b], "first-instruction zero x87/MMX payloads");
        for (uint32_t b = 160; b < 288; ++b) Require(!fx[b], "first-instruction all XMM payloads zero");
        Require(U16(env) == 0x37F && !U16(env+4) && U16(env+8) == 0xFFFF
            && !U32(env+12) && !U16(env+16) && !(U16(env+18) & 0x7FF)
            && !U32(env+20) && !U16(env+24), "independent FNSTENV canonical tags/pointers/opcode");
        Require(U32(observation+20) == 0, "user FP comparison never failed");
    }
    NativeStatus WaitStopped(NativeRuntime& runtime, TaskManager& tasks, uint32_t id) {
        const uint32_t begin = tasks.Ticks(); NativeStatus status = {};
        do {
            Require(runtime.Status(id, status), "status present");
            if (!status.live) return status;
            WaitTicks(tasks, 1);
        } while ((uint32_t)(tasks.Ticks() - begin) < 1500);
        Require(false, "native FP task stops before deadline"); return status;
    }
    void CheckPeer(NativeRuntime& runtime, uint32_t id) {
        NativeStatus status = {};
        Require(runtime.Status(id, status) && status.live, "FP survivor remains live");
        CheckFresh(runtime, id);
        if (U32(observation+28) != 15 || !U32(observation+12)) {
            printf((char*)"PEER NOT READY id="); printfHex32(id);
            printf((char*)" phase="); printfHex32(U32(observation+8));
            printf((char*)" flags="); printfHex32(U32(observation+28));
            printf((char*)" cycles="); printfHex32(U32(observation+12));
            printf((char*)" runTicks="); printfHex32(status.statistics.runTicks);
            printf((char*)" syscalls="); printfHex32(status.systemCalls); printf((char*)"\n");
        }
        Require(U32(observation+28) == 15 && U32(observation+12) > 0,
            "peer preserved every XMM/x87/MMX, syscalls/yield/DF/null selectors/EMMS");
        const uint32_t seed = U32(observation+4);
        const uint8_t* seeded = observation+2080;
        Require(U16(seeded) == ((seed & 1) ? 0x0B7F : 0x067F)
            && ((U16(seeded+2) >> 11) & 7) == ((seed & 1) ? 2U : 1U)
            && seeded[4] == ((seed & 1) ? 0xDF : 0xBF),
            "independent FCW precision/rounding, TOP and tag shapes actually seeded");
        for (uint32_t i = 0; i < 32; ++i)
            Require(U32(seeded+160+i*4) == seed * 0x1020304U + i * 0x01010101U,
                "every XMM lane actually seeded with independent pattern");
        Require(U32(observation+2592+12) >= NativeRuntime::CodeAddress
            && U32(observation+2592+20) >= NativeRuntime::DataAddress,
            "actual x87 instruction and data pointer history captured independently");
        Require(status.statistics.runTicks >= 3 && status.statistics.dispatches >= 3
            && status.statistics.yields >= 2 && (status.observedCs & 3) == 3
            && status.observedCr3 == status.directory, "peer genuine CPL3/private CR3/switch accounting");
    }
}
extern "C" void NativeFpSmoke(void* multiboot, uint32_t magic) {
    printf((char*)"NATIVE FP SMOKE BOOT\n");
    uint32_t maximum, vendorB, vendorC, vendorD;
    asm volatile("cpuid" : "=a"(maximum), "=b"(vendorB), "=c"(vendorC), "=d"(vendorD) : "a"(0), "c"(0));
    printf((char*)"CPUID.0 MAX="); printfHex32(maximum); printf((char*)" VENDOR EBX/EDX/ECX=");
    printfHex32(vendorB); printfHex32(vendorD); printfHex32(vendorC); printf((char*)"\n");
    const MultibootInfo* boot = (const MultibootInfo*)multiboot;
    const char* command = (boot->flags & 4) ? (const char*)boot->commandLine : "";
    const bool noFxsr = Contains(command, "case=no-fxsr");
    const bool noSse2 = Contains(command, "case=no-sse2");
    const bool noSse3 = Contains(command, "case=no-sse3");
    const bool sse2Only = Contains(command, "case=sse2");
    const bool inheritedXsave = Contains(command, "case=osxsave");
    const bool kernelMisuse = Contains(command, "case=kernel-misuse");
    const bool requireAvx = Contains(command, "case=avx");
    const bool sse3 = noSse3 || Contains(command, "case=sse3");
    uint32_t a, b, c, d;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    printf((char*)"CPUID.1 ECX="); printfHex32(c); printf((char*)" EDX="); printfHex32(d); printf((char*)"\n");
    Require(!noFxsr || !(d & (1U << 24)), "negative CPU actually lacks FXSR");
    Require(!noSse2 || !(d & (1U << 26)), "negative CPU actually lacks SSE2");
    Require(!(noSse3 || sse2Only) || !(c & 1), "SSE3-absent CPU actually lacks SSE3");
    Require(!requireAvx || (c & (1U << 28)), "AVX-negative test CPU actually advertises AVX");
    Require(!inheritedXsave || (c & (1U << 26)), "OSXSAVE-inheritance CPU advertises XSAVE");
    GlobalDescriptorTable gdt;
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    InputIrq keyboard(interrupts, 0x21), mouse(interrupts, 0x2C);
    PhysicalMemoryManager frames;
    Require(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frames initialize");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end, boot, 0, 0};
    Require(paging.prepareIdentity(frames, config), "identity prepare");
    NativeRuntime runtime;
    Require(runtime.PrepareStacks(paging, frames), "retained guard stacks before sharing");
    Require(paging.enable() && paging.sealForSharedProcessors(), "enable and seal template");
    expectedCr0 = Cr0() | (1U << 18); // AM, with user/kernel AC clear.
    expectedCr4 = Cr4() | ((d & (1U << 13)) ? (1U << 7) : 0); // PGE if supported.
    if (inheritedXsave) expectedCr4 |= 1U << 18;
    asm volatile("mov %0,%%cr0; mov %1,%%cr4" : : "r"(expectedCr0), "r"(expectedCr4) : "memory");
    const uint32_t baseline = frames.getStatistics().freeFrames;
    const bool activated = runtime.Activate(tasks, gdt, paging, frames, sse3 ? NativeFpSse3 : NativeFpSse2);
    Task ring0(&gdt, Ring0Task); Require(tasks.AddTask(&ring0), "integer-only ring0 peer");
    Out(0x43, 0x36); Out(0x40, 11932 & 255); Out(0x40, 11932 >> 8);
    if (noFxsr || noSse2 || noSse3 || inheritedXsave) {
        Require(!activated && !NativeRuntime::Active() && !runtime.FpEnabled()
            && runtime.FpError() == (inheritedXsave ? NativeFpExtendedState : NativeFpMissingFeature),
            "unsupported profile rejected with specific error and no publication");
        Require(Cr0() == expectedCr0 && Cr4() == expectedCr4
            && frames.getStatistics().freeFrames == baseline && tasks.TaskCount() == 1,
            "feature rejection changes no controls, frames, or native runnable task");
        interrupts.Activate(); WaitTicks(tasks, 10);
        Require(ring0Progress && tasks.BootTicks(), "kernel remains responsive after feature rejection");
        printf((char*)"NATIVE FP CAPABILITY REJECTION PASS\n"); Finish(true);
    }
    Require(activated, "activate enabled native FP profile"); CheckControls();
    if (kernelMisuse) {
        interrupts.Activate(); asm volatile("cli" : : : "memory");
        printf((char*)"NATIVE FP EXPECT CPL0 NM\n");
        native_fp_test_kernel_misuse();
        Require(false, "kernel FP instruction must panic");
    }
    pointerLimitedDiagnostic = Contains(command, "allow-emulator-pointer-limit=1");
    xmLimitedDiagnostic = Contains(command, "allow-emulator-xm-limit=1");
    mxcsrLimitedDiagnostic = Contains(command, "allow-emulator-mxcsr-limit=1");
    if (pointerLimitedDiagnostic)
        printf((char*)"DIAGNOSTIC ONLY: independently missing emulator pointer restoration may be waived\n");
    InitInput();
    const uint32_t first = Create(runtime, 0, 17), second = Create(runtime, 0, 42);
    NativeStatus one = {}, two = {};
    bool sawFirstTimer = false, sawSecondTimer = false;
    printf((char*)"NATIVE FP IRQ WINDOW\n");
    interrupts.Activate();
    uint32_t start = tasks.Ticks();
    do {
        Require(runtime.Status(first, one) && runtime.Status(second, two), "independent users status");
        if (!one.live || !two.live) {
            const uint32_t bad = one.live ? second : first;
            Read(runtime, bad);
            printf((char*)"USER FAILED id="); printfHex32(bad);
            printf((char*)" phase="); printfHex32(U32(observation+8));
            printf((char*)" failure="); printfHex32(U32(observation+20));
            printf((char*)" independent FLDENV missing-fields="); printfHex32(U32(observation+44)); printf((char*)"\n");
            for (uint32_t offset = 0; offset < 544; ++offset) {
                if (observation[608+offset] != observation[1152+offset]) {
                    printf((char*)"STATE DIFF +"); printfHex32(offset);
                    printf((char*)" expected="); printfHex32(observation[608+offset]);
                    printf((char*)" actual="); printfHex32(observation[1152+offset]); printf((char*)"\n");
                }
            }
        }
        Require(one.live && two.live, "independent users survive initial timer-only window");
        sawFirstTimer |= one.statistics.runTicks >= 3 && one.systemCalls == 0;
        sawSecondTimer |= two.statistics.runTicks >= 3 && two.systemCalls == 0;
        Read(runtime, first); const bool firstReady = U32(observation+12) > 0;
        Read(runtime, second); const bool secondReady = U32(observation+12) > 0;
        if (firstReady && secondReady && keyboard.user && mouse.user) break;
        WaitTicks(tasks, 1);
    } while ((uint32_t)(tasks.Ticks() - start) < 1500);
    Require(sawFirstTimer && sawSecondTimer, "both seeded FP users truly timer-preempted before any syscall");
    Require(one.directory != two.directory && tasks.ContextSwitches() >= 10
        && tasks.BootTicks() >= 3 && ring0Progress, "independent CR3s and boot/ring0/scheduler progress");
    CheckPeer(runtime, first); CheckPeer(runtime, second); CheckControls();
    Require(keyboard.user && mouse.user, "real nonscheduling keyboard and mouse IRQs interrupted CPL3");
    if (pointerLimitedDiagnostic) {
        Read(runtime, first); const uint32_t firstMissing = U32(observation+44);
        Read(runtime, second); const uint32_t secondMissing = U32(observation+44);
        Require(firstMissing == 15 && secondMissing == 15,
            "diagnostic waiver justified by independent FLDENV pointer self-test in both peers");
        printf((char*)"EMULATOR LIMITATION: user FLDENV ignores FIP/FCS/FDP/FDS; kernel pointer fidelity UNPROVEN\n");
    }
    printf((char*)"NATIVE FP PAYLOADS CONTROLS TIMER SYSCALL YIELD MMX IRQ PASS\n");
    printf((char*)"CPL3 IRQ keyboard="); printfHex32(keyboard.user); printf((char*)" mouse="); printfHex32(mouse.user); printf((char*)"\n");
    const uint32_t peerBaseline = frames.getStatistics().freeFrames;
    asm volatile("cli" : : : "memory");
    const uint32_t third = Create(runtime, 0, 81), fourth = Create(runtime, 0, 82);
    const NativeFpStatistics beforeFailure = runtime.FpStatistics();
    const uint32_t fullFrames = frames.getStatistics().freeFrames;
    NativeImage fullImage = {&native_fp_user_start, (uint32_t)(&native_fp_user_end-&native_fp_user_start),
        0, userData, sizeof(userData)};
    uint32_t rejectedId = 99;
    Require(!runtime.Create(fullImage, rejectedId) && !rejectedId
        && frames.getStatistics().freeFrames == fullFrames
        && runtime.FpStatistics().initialized == beforeFailure.initialized,
        "full admission fails before FP initialization and without allocation leak");
    Require(runtime.RequestExit(third, 0) && runtime.RequestExit(fourth, 0)
        && runtime.Reap() == 2 && frames.getStatistics().freeFrames == peerBaseline,
        "never-dispatched FP records cancelled and scrubbed without extra tasks");
    asm volatile("sti" : : : "memory");
    // Reused third slot alternates dirty normal exit, fault, cancellation, fresh.
    const uint32_t modes[] = {13, 1, 14, 1, 2, 1, 16, 3, 1, 4, 5, 6, 7, 8, 17, 9, 10, 11, 12};
    for (uint32_t round = 0; round < 2; ++round) {
        for (uint32_t n = 0; n < sizeof(modes)/sizeof(modes[0]); ++n) {
            const uint32_t mode = modes[n];
            if (mode == 10 && !requireAvx) continue;
            const uint32_t id = Create(runtime, mode, 71 + round);
            NativeStatus result = WaitStopped(runtime, tasks, id);
            const uint32_t vector = mode == 2 ? 16 : mode == 3 || mode == 16 ? 19
                : mode == 4 || mode == 5 || mode == 9 || mode == 17 ? 13
                : mode == 6 || mode == 7 || mode == 8 || mode == 11 ? 14
                : mode == 10 || mode == 12 ? 6 : 0;
            Read(runtime, id);
            const bool missingXm = xmLimitedDiagnostic && (mode == 3 || mode == 16)
                && result.exitCode == 0xF019 && !result.faultVector
                && U32(observation+8) == 8 && U32(observation+40) == 0x1F01;
            const bool missingMxcsr = mxcsrLimitedDiagnostic && (mode == 9 || mode == 17)
                && result.exitCode == 0xF00D && !result.faultVector
                && U32(observation+8) == 1 && (U32(observation+40) & 0x80000000U);
            if (missingMxcsr)
                printf(mode == 9 ? (char*)"EMULATOR LIMITATION: FXRSTOR accepts reserved MXCSR bit31; reserved-image GP UNPROVEN\n"
                    : (char*)"EMULATOR LIMITATION: LDMXCSR accepts reserved MXCSR bit31; reserved-operand GP UNPROVEN\n");
            if (missingXm)
                printf(mode == 16 ? (char*)"EMULATOR LIMITATION: direct user DIVPS sets unmasked invalid status but delivers no XM\n"
                    : (char*)"EMULATOR LIMITATION: restored unmasked DIVPS delivers no XM; XM containment UNPROVEN\n");
            if (!missingXm && !missingMxcsr && result.exitCode != (vector ? 0x80000000U | vector : 0)) {
                printf((char*)"MODE="); printfHex32(mode); printf((char*)" EXIT="); printfHex32(result.exitCode);
                printf((char*)" VECTOR="); printfHex32(result.faultVector);
                Read(runtime, id); printf((char*)" PHASE="); printfHex32(U32(observation+8));
                printf((char*)" MXCSR="); printfHex32(U32(observation+40)); printf((char*)"\n");
            }
            Require(missingXm || missingMxcsr || (result.exitCode == (vector ? 0x80000000U | vector : 0)
                && result.faultVector == vector), "fault only victim with exact expected vector");
            CheckFresh(runtime, id);
            if (mode == 2) Require(U32(observation+8) == 7 && result.statistics.runTicks >= 3
                && result.statistics.yields == 1 && result.systemCalls == 2,
                "pending x87 survived syscall, yield and true preemption before user FWAIT");
            if (mode == 3 || mode == 16)
                Require(U32(observation+8) == 8, "SIMD arithmetic reached with unmasked MXCSR");
            if (mode == 8) Require(result.faultAddress == (uint32_t)supervisorTarget
                && (result.faultError & 7) == 7, "supervisor FP operand rejected as user write");
            Require(runtime.Reap() == 1 && frames.getStatistics().freeFrames == peerBaseline
                && tasks.TaskCount() == 3, "fault/exit reap exact allocator and runnable baseline");
            CheckPeer(runtime, first); CheckPeer(runtime, second); CheckControls();
        }
        uint32_t cancelled = Create(runtime, 0, 99);
        WaitTicks(tasks, 15); CheckFresh(runtime, cancelled);
        Require(runtime.RequestExit(cancelled, 55) && runtime.Reap() == 1
            && frames.getStatistics().freeFrames == peerBaseline, "inactive dirty FP owner cancellation and exact reap");
        uint32_t clean = Create(runtime, 1, 111);
        Require(WaitStopped(runtime, tasks, clean).exitCode == 0, "fresh process after cancellation exits normally");
        CheckFresh(runtime, clean);
        Require(runtime.Reap() == 1 && frames.getStatistics().freeFrames == peerBaseline, "fresh cancellation reuse exact reap");
    }
    printf((char*)"NATIVE FP FRESH REUSE MF BAD OPERANDS CHURN PASS\n");
    if (requireAvx) printf((char*)"NATIVE FP AVX HARDWARE PRESENT OSXSAVE OFF USER UD PASS\n");
    Require(runtime.RequestExit(first, 0) && runtime.RequestExit(second, 0)
        && runtime.Reap() == 2 && frames.getStatistics().freeFrames == baseline
        && tasks.TaskCount() == 1, "all FP processes reap to exact original frame baseline");
    WaitTicks(tasks, 8); CheckControls();
    Require(ring0Progress && tasks.BootTicks(), "boot and integer kernel task survive final reap");
    const NativeFpStatistics fp = runtime.FpStatistics();
    Require(fp.saves && fp.saves == fp.restores && fp.initialized == fp.invalidated
        && fp.initialized == runtime.Statistics().created && !fp.invariantFailures,
        "every user return saved exactly once and every initialized record invalidated");
    Require(fp.userKeyboardInterrupts == keyboard.user && fp.userMouseInterrupts == mouse.user
        && fp.userKeyboardInterrupts && fp.userMouseInterrupts,
        "ownership-verified user IRQ counters exactly match independent interrupt handlers");
    printf((char*)"FP saves/restores="); printfHex32(fp.saves);
    printf((char*)" init/reap="); printfHex32(fp.initialized); printf((char*)"\n");
    printf((char*)"NATIVE FP FINAL ALLOCATOR CONTROL KERNEL PROGRESS PASS\n");
    Finish(true);
}
