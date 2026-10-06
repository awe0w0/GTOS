// Rootless i386 tests of the common frame + timer-independent dispatch contract.
#define private public
#include <multitasking.h>
#undef private
#include <process/abi.h>
#include <process/fault_policy.h>
#include "native_process_probe_expectations.h"
using namespace gtos;
uint16_t GlobalDescriptorTable::CodeSegmentSelector() { return 0x10; }
namespace {
    uint32_t failures;
    void Print(const char* text) {
        uint32_t length = 0; while (text[length]) ++length;
        uint32_t result;
        asm volatile("int $0x80" : "=a"(result) : "0"(4), "b"(1), "c"(text), "d"(length) : "memory", "cc");
    }
    void Check(bool value, const char* why) { if (!value) { ++failures; Print("FAIL "); Print(why); Print("\n"); } }
    void Entry() {}
}
extern "C" int NativeTestsMain() {
    Check(sizeof(CPUState) == 72 && __builtin_offsetof(CPUState, gs) == 0
        && __builtin_offsetof(CPUState, ds) == 12
        && __builtin_offsetof(CPUState, cs) == 56
        && __builtin_offsetof(CPUState, ss) == 68, "common trap frame offsets");
    TaskManager tasks;
    Task first((uint16_t)0x10, Entry), second((uint16_t)0x10, Entry);
    CPUState boot = {}, firstFrame = {}, secondFrame = {};
    Check(first.cpustate->ds == 0x18 && first.cpustate->es == 0x18
        && first.cpustate->fs == 0x18 && first.cpustate->gs == 0x18
        && first.cpustate->cs == 0x10 && first.cpustate->eflags == 0x202,
        "kernel bootstrap normalizes segments and flags");
    Check(((uint32_t)first.cpustate + 64) % 16 == 12
        && first.cpustate->ss == (uint32_t)&first, "cdecl bootstrap tail preserved");
    Check(!first.UserMode() && tasks.AddTask(&first) && tasks.AddTask(&second), "legacy task add");
    Check(tasks.Schedule(&boot) == first.cpustate && tasks.Ticks() == 1, "timer starts first");
    Check(tasks.Reschedule(&firstFrame) == second.cpustate && tasks.Ticks() == 1
        && first.Statistics().runTicks == 0, "yield dispatch cannot invent ticks");
    Check(tasks.SleepTask(&first, 2), "sleep first");
    Check(tasks.Reschedule(&secondFrame) == &boot && tasks.Ticks() == 1, "exit route reaches boot without tick");
    Check(tasks.Reschedule(&boot) == &secondFrame && first.State() == TaskSleeping
        && tasks.Ticks() == 1, "repeated dispatch does not wake sleeping task");
    Check(tasks.Schedule(&secondFrame) == &boot && tasks.Ticks() == 2, "second timer");
    Check(tasks.Schedule(&boot) == &firstFrame && tasks.Ticks() == 3, "real deadline wakes first");
    Check(tasks.TerminateTask(&first) && tasks.Reschedule(&firstFrame) == &secondFrame
        && tasks.RemoveTask(&first), "deferred removal after context selection");
    Check(GTOS_SYS_WRITE != 4 && GTOS_NATIVE_WRITE_LIMIT == 256
        && GTOS_ERR_BAD_ADDRESS == -14 && GTOS_ERR_UNSUPPORTED == -38, "bounded versioned ABI");
    CPUState fault = {}; fault.cs = 0x23;
    for (uint32_t vector = 0; vector < 256; ++vector) {
        fault.vector = vector; fault.error = 4;
        const bool supported = vector == 0 || vector == 1 || (vector >= 3 && vector <= 7)
            || (vector >= 10 && vector <= 14) || vector == 16 || vector == 17 || vector == 19;
        Check(process::RecoverableUserFault(fault) == supported, "user fault allowlist excludes catastrophic and unknown vectors");
        fault.cs = 0x10;
        Check(!process::RecoverableUserFault(fault), "kernel origin always fatal");
        fault.cs = 0x23;
    }
    fault.vector = 14;
    for (uint32_t error = 0; error < 32; ++error) {
        fault.error = error;
        Check(process::RecoverableUserFault(fault) == ((error & 4) && !(error & 8)),
            "PF reserved-bit and implicit supervisor-access errors remain fatal");
    }
    process::NativeStatus legacy = {};
    legacy.id = 0x13; legacy.exitCode = 0x80000001U; legacy.faultVector = 1;
    legacy.observedCs = 0x23; legacy.observedCr3 = legacy.directory = 0x2000;
    legacy.observedEflags = 0x302; legacy.systemCalls = 1;
    Check(native_process_tests::LegacyMovSsDebug(18, legacy, 0x1000), "known user MOV-SS #DB outcome accepted");
    for (uint32_t mode = 0; mode < 32; ++mode)
        Check(native_process_tests::LegacyMovSsDebug(mode, legacy, 0x1000) == (mode == 18),
            "debug compatibility exception scoped exclusively to MOV-SS fixture");
    for (uint32_t mutation = 0; mutation < 15; ++mutation) {
        process::NativeStatus bad = legacy;
        switch (mutation) {
        case 0: bad.observedCs = 0x10; break;
        case 1: bad.observedCs = 0x13; break;
        case 2: bad.observedCr3 = 0x1000; break;
        case 3: bad.observedCr3 = 0x3000; break;
        case 4: bad.directory = bad.observedCr3 = 0; break;
        case 5: bad.faultVector = 13; break;
        case 6: bad.faultError = 1; break;
        case 7: bad.exitCode = 99; break;
        case 8: bad.observedEflags &= ~0x100U; break;
        case 9: bad.observedEflags |= 0x3000U; break;
        case 10: bad.systemCalls = 2; break;
        case 11: bad.live = true; break;
        case 12: bad.reaped = true; break;
        case 13: bad.faultAddress = 0x40000000U; break;
        case 14: bad.id = 0; break;
        }
        Check(!native_process_tests::LegacyMovSsDebug(18, bad, 0x1000),
            "debug compatibility cannot admit wrong origin, CR3, flags, lifecycle or fault");
    }
    process::NativeStatus sse = legacy;
    sse.exitCode = 0x80000007U; sse.faultVector = 7; sse.observedEflags = 0x202;
    Check(native_process_tests::LegacySsePriority(12, false, sse, 0x1000),
        "known OSFXSR-off SSE #NM priority accepted");
    for (uint32_t mode = 0; mode < 32; ++mode) {
        Check(native_process_tests::LegacySsePriority(mode, false, sse, 0x1000) == (mode == 12),
            "SSE priority compatibility scoped exclusively to SSE fixture");
        Check(!native_process_tests::LegacySsePriority(mode, true, sse, 0x1000),
            "SSE priority alternate excludes OSFXSR-on state");
    }
    for (uint32_t mutation = 0; mutation < 15; ++mutation) {
        process::NativeStatus bad = sse;
        switch (mutation) {
        case 0: bad.observedCs = 0x10; break;
        case 1: bad.observedCs = 0x13; break;
        case 2: bad.observedCr3 = 0x1000; break;
        case 3: bad.observedCr3 = 0x3000; break;
        case 4: bad.directory = bad.observedCr3 = 0; break;
        case 5: bad.faultVector = 6; break;
        case 6: bad.faultError = 1; break;
        case 7: bad.exitCode = 99; break;
        case 8: bad.observedEflags |= 0x100U; break;
        case 9: bad.observedEflags |= 0x3000U; break;
        case 10: bad.systemCalls = 2; break;
        case 11: bad.live = true; break;
        case 12: bad.reaped = true; break;
        case 13: bad.faultAddress = 0x40000000U; break;
        case 14: bad.id = 0; break;
        }
        Check(!native_process_tests::LegacySsePriority(12, false, bad, 0x1000),
            "SSE priority alternate rejects wrong origin, CR3, flags, lifecycle or fault");
    }
    process::NativeStatus sysret = sse;
    sysret.exitCode = 0x80000006U; sysret.faultVector = 6;
    Check(native_process_tests::LegacySysretPriority(27, true, true, sysret, 0x1000),
        "known disabled-SCE SYSRET #UD priority accepted");
    for (uint32_t mode = 0; mode < 32; ++mode) {
        Check(native_process_tests::LegacySysretPriority(mode, true, true, sysret, 0x1000) == (mode == 27),
            "SYSRET priority compatibility scoped exclusively to SYSRET fixture");
        Check(!native_process_tests::LegacySysretPriority(mode, false, true, sysret, 0x1000)
            && !native_process_tests::LegacySysretPriority(mode, true, false, sysret, 0x1000),
            "SYSRET alternate requires supported and verified-disabled SCE");
    }
    for (uint32_t mutation = 0; mutation < 15; ++mutation) {
        process::NativeStatus bad = sysret;
        switch (mutation) {
        case 0: bad.observedCs = 0x10; break;
        case 1: bad.observedCs = 0x13; break;
        case 2: bad.observedCr3 = 0x1000; break;
        case 3: bad.observedCr3 = 0x3000; break;
        case 4: bad.directory = bad.observedCr3 = 0; break;
        case 5: bad.faultVector = 13; break;
        case 6: bad.faultError = 1; break;
        case 7: bad.exitCode = 99; break;
        case 8: bad.observedEflags |= 0x100U; break;
        case 9: bad.observedEflags |= 0x3000U; break;
        case 10: bad.systemCalls = 2; break;
        case 11: bad.live = true; break;
        case 12: bad.reaped = true; break;
        case 13: bad.faultAddress = 0x40000000U; break;
        case 14: bad.id = 0; break;
        }
        Check(!native_process_tests::LegacySysretPriority(27, true, true, bad, 0x1000),
            "SYSRET alternate rejects wrong origin, CR3, flags, lifecycle or fault");
    }
    if (!failures) Print("PASS: native trap layout, segment bootstrap, timer-only ticks, deferred removal, fault policy and bounded emulator compatibility\n");
    return failures ? 1 : 0;
}
asm(".global _start\n_start:\n andl $-16, %esp\n call NativeTestsMain\n movl %eax, %ebx\n movl $1, %eax\n int $0x80\n");
