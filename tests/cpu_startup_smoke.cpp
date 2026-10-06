#include <hardwarecommunication/cpu_startup.h>
#include <gdt.h>
using namespace gtos;
using namespace gtos::hardwarecommunication;
extern "C" uint8_t kernel_start, kernel_end;
namespace {
    void Print(const char* text) { while (*text) { asm volatile("outb %0,$0xE9" : : "a"(*text)); ++text; } }
    void Hex(uint32_t value) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            char c = "0123456789ABCDEF"[(value >> shift) & 15]; asm volatile("outb %0,$0xE9" : : "a"(c));
        }
    }
    void Value(const char* name, uint32_t value) { Print(name); Hex(value); Print("\n"); }
    void Finish(bool passed) __attribute__((noreturn));
    void Finish(bool passed) {
        Print(passed ? "AP SMOKE PASS\n" : "AP SMOKE FAIL\n");
        asm volatile("outl %0,%1" : : "a"(passed ? 0x10U : 0x20U), "Nd"((uint16_t)0xF4));
        for (;;) asm volatile("cli; hlt");
    }
    bool Contains(const char* text, const char* word) {
        if (!text) return false;
        for (; *text; ++text) {
            uint32_t i = 0; while (word[i] && text[i] == word[i]) ++i;
            if (!word[i]) return true;
        }
        return false;
    }
    uint32_t Expected(const char* text) {
        if (!text) return 0;
        for (; *text; ++text) if (text[0] == 'n' && text[1] == '=') {
            uint32_t n = 0; text += 2;
            while (*text >= '0' && *text <= '9') { n = n * 10 + *text - '0'; ++text; }
            return n;
        }
        return 0;
    }
    void Put32(uint8_t* p, uint32_t value) { for (uint32_t i = 0; i < 4; ++i) p[i] = value >> (i * 8); }
}
extern "C" void CpuStartupSmoke(void* multiboot, uint32_t magic) {
    Print("AP SMOKE BOOT\n");
    GlobalDescriptorTable gdt;
    memory::PhysicalMemoryManager frames;
    if (!frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end)) {
        Value("MEMORY ERROR ", frames.getLastError()); Finish(false);
    }
    const memory::MultibootInfo& info = *(const memory::MultibootInfo*)multiboot;
    const char* cmdline = (info.flags & 4) ? (const char*)info.commandLine : 0;
    uint32_t limit = ((info.memUpper + 1024 + 1023) / 1024) * 1024 * 1024;
    CpuManager cpu; cpu.Detect(limit);
    Value("CPU DETECTED ", cpu.DetectedLogicalProcessors());
    Value("CPU ONLINE ", cpu.OnlineProcessors());
    Print(cpu.EnumerationSourceName()); Print("\n");
    CpuInfo startupInfo = cpu.GetInfo();
    bool timeoutTest = Contains(cmdline, "timeout"), rejectTest = Contains(cmdline, "reject");
    uint8_t fakeTable[60];
    if (timeoutTest) {
        for (uint32_t i = 0; i < sizeof(fakeTable); ++i) fakeTable[i] = 0;
        fakeTable[0] = 'A'; fakeTable[1] = 'P'; fakeTable[2] = 'I'; fakeTable[3] = 'C';
        Put32(fakeTable + 4, sizeof(fakeTable)); fakeTable[8] = 5;
        Put32(fakeTable + 36, 0xFEE00000U);
        for (uint32_t i = 0; i < 2; ++i) {
            fakeTable[44 + i * 8] = 0; fakeTable[45 + i * 8] = 8;
            fakeTable[47 + i * 8] = i ? 127 : startupInfo.bspApicId;
            Put32(fakeTable + 48 + i * 8, 1);
        }
        uint8_t sum = 0; for (uint32_t i = 0; i < sizeof(fakeTable); ++i) sum += fakeTable[i];
        fakeTable[9] = (uint8_t)(0 - sum);
        startupInfo.firmwareTableAddress = (uint32_t)fakeTable;
        startupInfo.enumerationSource = CpuAcpiMadt;
        startupInfo.detectedLogicalProcessors = 2;
    }
    if (rejectTest) startupInfo.featureEdx &= ~(1U << 9);
    CpuStartup startup;
    bool started = startup.Start(startupInfo, frames, limit);
    CpuStartupReport report = startup.GetReport();
    Print("STARTUP "); Print(CpuStartup::ErrorName(report.error)); Print("\n");
    Value("AP ATTEMPTED ", report.attemptedAps); Value("AP ACKNOWLEDGED ", report.acknowledgedAps);
    Value("AP PARKED ", report.parkedAps); Value("AP FAILED ", report.failedAps);
    Value("TRAMPOLINE ", report.trampolineAddress);
    for (uint32_t i = 0; i < report.detectedProcessors; ++i) {
        CpuStartupProcessorInfo processor;
        if (!startup.GetProcessor(i, processor)) continue;
        Value("APIC ID ", processor.apicId); Value("STATE ", processor.state);
        Value("STACK ", processor.stackBase); Value("OBSERVED SP ", processor.observedStackPointer);
        Value("CHECKSUM ", processor.selfTestChecksum);
    }
    uint32_t expected = Expected(cmdline);
    bool inventoryOk = !expected || expected == cpu.DetectedLogicalProcessors();
    bool safe = report.schedulerOnlineProcessors == 1 && cpu.OnlineProcessors() == 1;
    if (timeoutTest) {
        CpuStartupProcessorInfo missing;
        bool retained = startup.GetProcessor(1, missing) && missing.state == CpuStartupTimedOut
            && frames.isAllocated(missing.stackBase) && frames.isBootstrapPage(report.trampolineAddress);
        Finish(inventoryOk && safe && retained && !started && report.error == CpuStartupPartialFailure
            && report.attemptedAps == 1 && !report.parkedAps && report.failedAps == 1);
    }
    if (rejectTest) {
        Finish(inventoryOk && safe && !started && report.error == CpuStartupNoLocalApic && !report.attemptedAps);
    }
    Finish(inventoryOk && safe && started && !report.failedAps
        && report.parkedAps + 1 == cpu.DetectedLogicalProcessors());
}
