// Rootless i386 tests of the common frame + timer-independent dispatch contract.
#define private public
#include <multitasking.h>
#undef private
#include <process/abi.h>
#include <process/fault_policy.h>
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
    if (!failures) Print("PASS: native trap layout, segment bootstrap, timer-only ticks, deferred removal and fail-closed exception classification\n");
    return failures ? 1 : 0;
}
asm(".global _start\n_start:\n andl $-16, %esp\n call NativeTestsMain\n movl %eax, %ebx\n movl $1, %eax\n int $0x80\n");
