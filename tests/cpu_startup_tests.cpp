#include <hardwarecommunication/cpu_startup.h>
using namespace gtos::hardwarecommunication;
extern "C" bool GtosApObservationSelfTest();
namespace {
    int failures = 0;
    void Print(const char* text) {
        uint32_t n = 0; while (text[n]) ++n;
        { uint32_t written; asm volatile("int $0x80" : "=a"(written) : "0"(4), "b"(1), "c"(text), "d"(n)  : "memory", "cc"); }
    }
    void Check(bool value, const char* name) {
        if (!value) { ++failures; Print("FAIL: "); Print(name); Print("\n"); }
    }
    void Zero(uint8_t* p, uint32_t n) { for (uint32_t i = 0; i < n; ++i) p[i] = 0; }
    void Put32(uint8_t* p, uint32_t value) { for (uint32_t i = 0; i < 4; ++i) p[i] = value >> (i * 8); }
    void Checksum(uint8_t* p, uint32_t n, uint32_t offset) {
        p[offset] = 0; uint8_t sum = 0;
        for (uint32_t i = 0; i < n; ++i) sum += p[i];
        p[offset] = (uint8_t)(0 - sum);
    }
    void Madt(uint8_t* p, uint32_t length) {
        Zero(p, length); p[0] = 'A'; p[1] = 'P'; p[2] = 'I'; p[3] = 'C';
        Put32(p + 4, length); Put32(p + 36, 0xFEE00000U);
        for (uint32_t i = 0; i < 2; ++i) {
            p[44 + i * 8] = 0; p[45 + i * 8] = 8; p[47 + i * 8] = i;
            Put32(p + 48 + i * 8, 1);
        }
    }
}
extern "C" int ApUnitMain() {
    Check(GtosApObservationSelfTest(), "late AP observations independently reverified");
    uint8_t table[128];
    CpuManager manager;
    CpuInfo cpu = manager.GetInfo();
    cpu.bspApicId = 0; cpu.detectedLogicalProcessors = 2;
    cpu.enumerationSource = CpuAcpiMadt; cpu.firmwareTableAddress = (uint32_t)table;
    uint32_t ids[256], count, lapic;
    Madt(table, 60); Checksum(table, 60, 9);
    Check(CpuStartup::ReadFirmwareInventory(cpu, 0xFFFFFFFFU, ids, 256, count, lapic)
        && count == 2 && ids[0] == 0 && ids[1] == 1 && lapic == 0xFEE00000U, "validated MADT inventory");
    Check(!CpuStartup::ReadFirmwareInventory(cpu, 0, ids, 256, count, lapic), "physical bound precedes table read");
    Check(!CpuStartup::ReadFirmwareInventory(cpu, (uint32_t)table + 59, ids, 256, count, lapic), "firmware span overrun rejected");
    Check(!CpuStartup::ReadFirmwareInventory(cpu, 0xFFFFFFFFU, ids, 1, count, lapic), "inventory capacity bound");
    Check(!CpuStartup::ReadFirmwareInventory(cpu, 0xFFFFFFFFU, 0, 256, count, lapic), "null inventory output rejected");
    cpu.bspApicId = 5;
    Check(!CpuStartup::ReadFirmwareInventory(cpu, 0xFFFFFFFFU, ids, 256, count, lapic), "missing BSP prevents startup");
    cpu.bspApicId = 0; cpu.detectedLogicalProcessors = 3;
    Check(!CpuStartup::ReadFirmwareInventory(cpu, 0xFFFFFFFFU, ids, 256, count, lapic), "inconsistent discovered count rejected");
    cpu.detectedLogicalProcessors = 2;
    table[9] ^= 1;
    Check(!CpuStartup::ReadFirmwareInventory(cpu, 0xFFFFFFFFU, ids, 256, count, lapic), "MADT checksum rechecked before IPI");
    Madt(table, 72); table[60] = 5; table[61] = 12; Put32(table + 64, 0xFEC00000U); Checksum(table, 72, 9);
    Check(CpuStartup::ReadFirmwareInventory(cpu, 0xFFFFFFFFU, ids, 256, count, lapic)
        && lapic == 0xFEC00000U, "32-bit LAPIC address override");
    Put32(table + 68, 1); Checksum(table, 72, 9);
    Check(!CpuStartup::ReadFirmwareInventory(cpu, 0xFFFFFFFFU, ids, 256, count, lapic), "above-4GiB LAPIC override rejected");
    Madt(table, 62); table[60] = 5; table[61] = 2; Checksum(table, 62, 9);
    Check(!CpuStartup::ReadFirmwareInventory(cpu, 0xFFFFFFFFU, ids, 256, count, lapic), "short LAPIC override rejected");
    Madt(table, 84); table[60] = table[72] = 5; table[61] = table[73] = 12;
    Put32(table + 64, 0xFEE00000U); Put32(table + 76, 0xFEE00000U); Checksum(table, 84, 9);
    Check(!CpuStartup::ReadFirmwareInventory(cpu, 0xFFFFFFFFU, ids, 256, count, lapic), "duplicate LAPIC override rejected");
    Zero(table, 84); table[0] = 'P'; table[1] = 'C'; table[2] = 'M'; table[3] = 'P';
    table[4] = 84; table[6] = 4; table[34] = 2; Put32(table + 36, 0xFEE00000U);
    table[45] = 0; table[47] = 3; table[65] = 1; table[67] = 1; Checksum(table, 84, 7);
    cpu.enumerationSource = CpuMpTable;
    Check(CpuStartup::ReadFirmwareInventory(cpu, 0xFFFFFFFFU, ids, 256, count, lapic) && count == 2,
          "MP inventory revalidated");
    Put32(table + 36, 0xFEE00001U); Checksum(table, 84, 7);
    Check(!CpuStartup::ReadFirmwareInventory(cpu, 0xFFFFFFFFU, ids, 256, count, lapic), "unaligned LAPIC base rejected");
    gtos::memory::PhysicalMemoryManager frames;
    CpuStartup startup;
    Check(!startup.Start(cpu, frames, 0xFFFFFFFFU)
        && startup.GetReport().error == CpuStartupInterruptsEnabled
        && !startup.GetReport().attemptedAps, "IF-enabled call rejected before privileged operations");
    Check(!startup.Start(cpu, frames, 0xFFFFFFFFU)
        && startup.GetReport().error == CpuStartupAlreadyRun, "repeated startup attempt rejected");
    if (!failures) Print("PASS: AP inventory validation and early startup guards\n");
    return failures ? 1 : 0;
}
asm(".global _start\n_start:\n andl $-16,%esp\n call ApUnitMain\n movl %eax,%ebx\n movl $1,%eax\n int $0x80\n");
