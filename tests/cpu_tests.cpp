// Freestanding i386 Linux test binary: no host libc or privileged instructions.
#define private public
#include <multitasking.h>
#undef private
#include <hardwarecommunication/cpu.h>

using namespace gtos;
using namespace gtos::hardwarecommunication;

uint16_t GlobalDescriptorTable::CodeSegmentSelector() { return 0x10; }
void* operator new(size_t, void* location) { return location; }

namespace {
    int failures = 0;
    int checks = 0;
    void Print(const char* text) {
        uint32_t length = 0;
        while (text[length]) ++length;
        asm volatile("int $0x80" : : "a"(4), "b"(1), "c"(text), "d"(length) : "memory");
    }
    void Check(bool passed, const char* name) {
        ++checks;
        if (!passed) { ++failures; Print("FAIL: "); Print(name); Print("\n"); }
    }
    void Put16(uint8_t* p, uint16_t value) { p[0] = value; p[1] = value >> 8; }
    void Put32(uint8_t* p, uint32_t value) {
        for (uint32_t i = 0; i < 4; ++i) p[i] = value >> (8 * i);
    }
    void Zero(uint8_t* p, uint32_t length) { for (uint32_t i = 0; i < length; ++i) p[i] = 0; }
    void Sum(uint8_t* p, uint32_t length, uint32_t checksumOffset) {
        p[checksumOffset] = 0;
        uint8_t sum = 0;
        for (uint32_t i = 0; i < length; ++i) sum += p[i];
        p[checksumOffset] = (uint8_t)(0 - sum);
    }
    void Madt(uint8_t* p, uint32_t length) {
        Zero(p, length); p[0] = 'A'; p[1] = 'P'; p[2] = 'I'; p[3] = 'C';
        Put32(p + 4, length); p[8] = 5;
    }
    void Lapic(uint8_t* p, uint8_t id, uint32_t flags) {
        p[0] = 0; p[1] = 8; p[2] = id; p[3] = id; Put32(p + 4, flags);
    }
    void X2apic(uint8_t* p, uint32_t id, uint32_t flags) {
        Zero(p, 16); p[0] = 9; p[1] = 16; Put32(p + 4, id); Put32(p + 8, flags);
    }
    void Mp(uint8_t* p, uint32_t length, uint16_t entries) {
        Zero(p, length); p[0] = 'P'; p[1] = 'C'; p[2] = 'M'; p[3] = 'P';
        Put16(p + 4, length); p[6] = 4; Put16(p + 34, entries);
    }
    void Processor(uint8_t* p, uint8_t id, uint8_t flags) {
        Zero(p, 20); p[1] = id; p[2] = 0x14; p[3] = flags;
    }
    void Entry() {}

    void TestCpuTables() {
        CpuManager cpu;
        Check(cpu.OnlineProcessors() == 1 && cpu.DetectedLogicalProcessors() == 1, "conservative boot defaults");
        cpu.Detect(0); // CPUID only; prohibit every physical firmware access.
        Check(cpu.GetInfo().cpuidAvailable && cpu.GetInfo().vendor[0], "CPUID vendor detection");
        Check(cpu.GetInfo().enumerationSource == CpuBootstrapOnly, "CPUID capacities are not detected CPU totals");
        uint8_t table[512];
        Madt(table, 76);
        for (uint32_t i = 0; i < 4; ++i) Lapic(table + 44 + 8 * i, i, 1);
        Sum(table, 76, 9);
        Check(cpu.DiscoverFromMadt(table, 76) && cpu.DetectedLogicalProcessors() == 4, "MADT four enabled processors");
        Check(cpu.OnlineProcessors() == 1, "MADT discovery never starts APs");
        Check(!cpu.DiscoverFromMadt(table, 75) && cpu.DetectedLogicalProcessors() == 4, "MADT truncation preserves state");
        table[9] ^= 1;
        Check(!cpu.DiscoverFromMadt(table, 76), "MADT bad checksum");
        Sum(table, 76, 9);
        table[45] = 0; Sum(table, 76, 9);
        Check(!cpu.DiscoverFromMadt(table, 76), "MADT zero-length entry rejected");
        table[45] = 40; Sum(table, 76, 9);
        Check(!cpu.DiscoverFromMadt(table, 76), "MADT overrun entry rejected");
        Madt(table, 84);
        Lapic(table + 44, 7, 1); Lapic(table + 52, 8, 2);
        X2apic(table + 60, 7, 1); Lapic(table + 76, 9, 0);
        Sum(table, 84, 9);
        Check(cpu.DiscoverFromMadt(table, 84) && cpu.DetectedLogicalProcessors() == 1,
              "MADT duplicate IDs and offline/hotplug-only CPUs excluded");
        Madt(table, 68); Lapic(table + 44, 1, 1); X2apic(table + 52, 0x1234, 1); Sum(table, 68, 9);
        Check(cpu.DiscoverFromMadt(table, 68) && cpu.DetectedLogicalProcessors() == 2, "MADT x2APIC 32-bit ID");
        Madt(table, 52); Lapic(table + 44, 0, 2); Sum(table, 52, 9);
        Check(!cpu.DiscoverFromMadt(table, 52), "MADT hotplug-only table rejected");
        Madt(table, 46); table[44] = 200; table[45] = 2; Sum(table, 46, 9);
        Check(!cpu.DiscoverFromMadt(table, 46), "MADT no enabled CPUs rejected");
        Check(!cpu.DiscoverFromMadt(0, 100), "MADT null rejected");

        Mp(table, 124, 4);
        for (uint32_t i = 0; i < 4; ++i) Processor(table + 44 + i * 20, i, i == 0 ? 3 : 1);
        Sum(table, 124, 7);
        Check(cpu.DiscoverFromMpTable(table, 124) && cpu.DetectedLogicalProcessors() == 4, "MP four enabled processors");
        Check(cpu.OnlineProcessors() == 1, "MP discovery never starts APs");
        Check(!cpu.DiscoverFromMpTable(table, 123), "MP truncated table rejected");
        table[7] ^= 1;
        Check(!cpu.DiscoverFromMpTable(table, 124), "MP bad checksum rejected");
        Sum(table, 124, 7);
        table[44] = 5; Sum(table, 124, 7);
        Check(!cpu.DiscoverFromMpTable(table, 124), "MP unknown entry type rejected");
        Mp(table, 104, 3); Processor(table + 44, 1, 1); Processor(table + 64, 1, 1);
        Processor(table + 84, 2, 0); Sum(table, 104, 7);
        Check(cpu.DiscoverFromMpTable(table, 104) && cpu.DetectedLogicalProcessors() == 1,
              "MP duplicate/disabled CPUs excluded");
        Put16(table + 34, 4); Sum(table, 104, 7);
        Check(!cpu.DiscoverFromMpTable(table, 104), "MP entry count exceeds buffer");
        Put16(table + 34, 2); Sum(table, 104, 7);
        Check(!cpu.DiscoverFromMpTable(table, 104), "MP unaccounted base bytes rejected");
        Check(!cpu.DiscoverFromMpTable(0, 100), "MP null rejected");

        // Mutated, truncated and random firmware is never allowed to mark APs
        // online or produce zero enabled CPUs after a successful parse.
        uint32_t seed = 123456789;
        bool invariant = true;
        for (uint32_t iteration = 0; iteration < 10000; ++iteration) {
            Madt(table, 76);
            for (uint32_t i = 0; i < 4; ++i) Lapic(table + 44 + 8 * i, i, 1);
            seed = seed * 1664525U + 1013904223U;
            table[seed % 76] ^= (uint8_t)(seed >> 24);
            if (iteration & 1) Sum(table, 76, 9);
            bool success = cpu.DiscoverFromMadt(table, iteration % 100);
            if (cpu.OnlineProcessors() != 1 || (success && !cpu.DetectedLogicalProcessors())) invariant = false;
        }
        Check(invariant, "10,000 malformed firmware probes preserve invariants");
    }

    void TestScheduler() {
        Check(TaskManager::RunSelfTests((GlobalDescriptorTable*)0x1000), "integrated scheduler self-test");
        TaskManager manager, other;
        Task first((uint16_t)0x10, Entry), second((uint16_t)0x10, Entry);
        Task noSelector((uint16_t)0, Entry), noEntry((uint16_t)0x10, 0);
        CPUState boot = {}, firstFrame = {}, secondFrame = {};
        Check(manager.Schedule(0) == 0 && manager.Ticks() == 0, "null IRQ frame does not advance clock");
        Check(!manager.AddTask(&noSelector) && !manager.AddTask(&noEntry), "invalid initial tasks rejected");
        Check(manager.AddTask(&first) && manager.AddTask(&second), "task admission");
        Check(!other.AddTask(&first), "task cannot belong to two schedulers");
        CPUState* start = manager.Schedule(&boot);
        Check(start && start->eip && start->vector == 0 && start->error == 0
            && start->esp == 0 && start->ss == (uint32_t)&first, "bootstrap frame and argument initialized");
        Check(((uint32_t)start + sizeof(CPUState) - 8) % 16 == 12, "bootstrap cdecl stack alignment");
        Check(start->eflags == 0x202 && start->cs == 0x10, "initial context flags and selector");
        Check(manager.CurrentTask() == &first && !manager.RemoveTask(&first), "running task cannot be removed");
        Check(!manager.SetAffinity(&first, 0) && !manager.SetAffinity(&first, 2)
            && !manager.SetAffinity(&first, 3), "offline or empty CPU affinity rejected");
        Check(manager.SetAffinity(&first, 1) && first.AffinityMask() == 1, "BSP affinity supported");
        Check(manager.Schedule(&firstFrame) && manager.CurrentTask() == &second, "round robin second task");
        Check(manager.Schedule(&secondFrame) == &boot && !manager.CurrentTask(), "GUI boot context remains runnable");
        Check(first.Statistics().runTicks == 1 && second.Statistics().runTicks == 1
            && manager.BootTicks() == 1 && manager.ContextSwitches() == 3, "runtime accounting");
        Check(!manager.SleepTask(&first, 0) && !manager.SleepTask(&first, 0x80000000U), "invalid sleep duration rejected");
        Check(manager.SleepTask(&first, 20) && manager.SleepTask(&second, 20), "sleep both tasks");
        Check(manager.Schedule(&boot) == &boot && manager.Schedule(&boot) == &boot, "all sleeping safely returns boot");
        Check(manager.WakeTask(&second) && !manager.WakeTask(&second), "explicit wake lifecycle");
        Check(manager.Schedule(&boot) == &secondFrame, "woken task dispatches");
        Check(manager.RemoveTask(&first) && manager.CurrentTask() == &second, "remove earlier slot preserves current identity");
        Check(manager.TerminateTask(&second) && !manager.TerminateTask(&second), "termination is one-way");
        Check(manager.Schedule(&secondFrame) == &boot && manager.RemoveTask(&second), "terminated task reaped after switch");
        Check(!manager.AddTask(&second) && !manager.WakeTask(&second), "terminated task cannot accidentally restart");

        TaskManager rollover;
        Task sleeper((uint16_t)0x10, Entry);
        rollover.AddTask(&sleeper);
        rollover.ticks = 0xFFFFFFFEU;
        Check(rollover.SleepTask(&sleeper, 3), "sleep across timer rollover admitted");
        Check(rollover.Schedule(&boot) == &boot && sleeper.State() == TaskSleeping, "rollover sleep first tick");
        Check(rollover.Schedule(&boot) == &boot && sleeper.State() == TaskSleeping, "rollover sleep zero tick");
        Check(rollover.Schedule(&boot) != &boot && sleeper.State() == TaskRunning && rollover.Ticks() == 1,
              "rollover sleep deadline wakes correctly");

        static uint8_t storage[257][sizeof(Task)] __attribute__((aligned(16)));
        TaskManager full;
        bool admission = true;
        for (int i = 0; i < 257; ++i) {
            Task* task = new (storage[i]) Task((uint16_t)0x10, Entry);
            if (full.AddTask(task) != (i < 256)) admission = false;
        }
        Check(admission && full.TaskCount() == 256, "256-task bound enforced");
    }
}
extern "C" int CpuTestsMain() {
    TestCpuTables();
    TestScheduler();
    if (!failures) Print("PASS: CPU discovery parsers, malformed firmware, scheduler lifecycle, affinity, rollover and capacity\n");
    return failures ? 1 : 0;
}
asm(".global _start\n_start:\n andl $-16, %esp\n call CpuTestsMain\n movl %eax, %ebx\n movl $1, %eax\n int $0x80\n");
