// Standalone pre-paging firmware observation. No CR/MSR/cache-control writes.
// Link the unmodified production CaptureMemoryTypes, not a reimplementation.
#include <hardwarecommunication/cpu_memory_types.h>
using namespace gtos;
using namespace gtos::hardwarecommunication;
namespace {
    void Text(const char* text) {
        while (*text) asm volatile("outb %0,$0xe9" : : "a"(*text++));
    }
    void Hex32(uint32_t value) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            const char c = "0123456789ABCDEF"[(value >> shift) & 15];
            asm volatile("outb %0,$0xe9" : : "a"(c));
        }
    }
    void Value(const char* label, uint32_t value) {
        Text(label); Text("="); Hex32(value); Text("\n");
    }
    void Value64(const char* label, uint64_t value) {
        Text(label); Text("="); Hex32((uint32_t)(value >> 32));
        Hex32((uint32_t)value); Text("\n");
    }
    uint32_t Cr0() { uint32_t v; asm volatile("mov %%cr0,%0" : "=r"(v) : : "memory"); return v; }
    uint32_t Cr4() { uint32_t v; asm volatile("mov %%cr4,%0" : "=r"(v) : : "memory"); return v; }
    uint32_t Flags() { uint32_t v; asm volatile("pushfl; popl %0" : "=r"(v) : : "memory"); return v; }
    bool CpuidAvailable() {
        uint32_t before, after;
        asm volatile("pushfl; popl %0; movl %0,%1; xorl $0x200000,%1;"
                     "pushl %1; popfl; pushfl; popl %1; pushl %0; popfl"
                     : "=&r"(before), "=&r"(after) : : "cc", "memory");
        return ((before ^ after) & 0x200000) != 0;
    }
    void Cpuid(uint32_t leaf, uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(leaf), "c"(0) : "memory");
        Text("CPUID."); Hex32(leaf); Text(" EAX="); Hex32(a);
        Text(" EBX="); Hex32(b); Text(" ECX="); Hex32(c);
        Text(" EDX="); Hex32(d); Text("\n");
    }
    uint64_t ReadMsr(uint32_t index) {
        uint32_t low, high;
        asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(index) : "memory");
        return ((uint64_t)high << 32) | low;
    }
}
extern "C" void NativeFpFirmwareProbe(uint32_t info, uint32_t magic) {
    Text("NATIVE FP FIRMWARE PROBE BEGIN\n");
    Value("MULTIBOOT_MAGIC", magic); Value("MULTIBOOT_INFO", info);
    const uint32_t cr0 = Cr0(), cr4 = Cr4(), flags = Flags();
    Value("CR0_BEFORE", cr0); Value("CR4_BEFORE", cr4); Value("EFLAGS_BEFORE", flags);
    Value("CR0_PE", (cr0 >> 0) & 1); Value("CR0_PG", (cr0 >> 31) & 1);
    Value("CR0_CD", (cr0 >> 30) & 1); Value("CR0_NW", (cr0 >> 29) & 1);
    Value("IF_CLEAR", !(flags & (1U << 9)));
    const bool cpuid = CpuidAvailable(); Value("CPUID_AVAILABLE", cpuid);
    if (!cpuid) { Text("NATIVE FP FIRMWARE PROBE FAIL no CPUID\n"); return; }
    uint32_t a, b, c, d;
    Cpuid(0, a, b, c, d);
    if (a < 1) { Text("NATIVE FP FIRMWARE PROBE FAIL no CPUID leaf 1\n"); return; }
    Cpuid(1, a, b, c, d);
    const uint32_t features = d;
    const bool msr = features & (1U << 5), apic = features & (1U << 9);
    const bool mtrr = features & (1U << 12), pat = features & (1U << 16);
    Value("CPUID_MSR", msr); Value("CPUID_APIC", apic);
    Value("CPUID_MTRR", mtrr); Value("CPUID_PAT", pat);
    bool bsp = false;
    if (msr && apic) {
        const uint64_t apicBase = ReadMsr(0x1B);
        Value64("IA32_APIC_BASE", apicBase);
        bsp = apicBase & (1U << 8); Value("VERIFIED_BSP", bsp);
    } else Text("IA32_APIC_BASE=SKIPPED missing MSR or APIC\n");
    Cpuid(0x80000000U, a, b, c, d);
    const uint32_t maxExtended = a;
    if (maxExtended >= 0x80000001U) Cpuid(0x80000001U, a, b, c, d);
    if (maxExtended >= 0x80000008U) {
        Cpuid(0x80000008U, a, b, c, d);
        Value("PHYSICAL_ADDRESS_BITS", a & 0xFF);
    } else Text("PHYSICAL_ADDRESS_BITS=production fallback 36\n");
    if (msr && mtrr) {
        Value64("IA32_MTRRCAP", ReadMsr(0xFE));
        Value64("IA32_MTRR_DEF_TYPE", ReadMsr(0x2FF));
    } else Text("MTRR_MSRS=SKIPPED missing MSR or MTRR\n");
    if (msr && pat) Value64("IA32_PAT", ReadMsr(0x277));
    else Text("IA32_PAT=SKIPPED missing MSR or PAT\n");

    const bool firstGateRejects = !(cr0 & 1U) || (cr0 & 0xE0000000U);
    Value("CAPTURE_FIRST_CR0_GATE_REJECTS", firstGateRejects);
    CpuMemoryTypes captured;
    captured.valid = 0xDEADBEEF;
    const bool capture = CaptureMemoryTypes(captured);
    Value("CAPTURE_RESULT", capture); Value("CAPTURE_VALID", captured.valid);
    if (capture) {
        Value("CAPTURE_PHYSICAL_ADDRESS_BITS", captured.physicalAddressBits);
        Value("CAPTURE_FEATURES", captured.cpuidFeatures);
        Value("CAPTURE_VARIABLE_COUNT", captured.variableCount);
        Value64("CAPTURE_MTRRCAP", captured.capability);
        Value64("CAPTURE_DEFAULT_TYPE", captured.defaultType);
        Value64("CAPTURE_PAT", captured.pat);
    }
    Value("CR0_AFTER", Cr0()); Value("CR4_AFTER", Cr4());
    const bool stable = Cr0() == cr0 && Cr4() == cr4 && !(Flags() & (1U << 9));
    Value("CONTROLS_UNCHANGED_IF_CLEAR", stable);
    const bool exactRejection = firstGateRejects && !capture && !captured.valid;
    Value("REJECTION_AT_FIRST_CR0_GATE_PROVEN", exactRejection);
    if (exactRejection)
        Text("LATER_MEMORY_TYPE_VALIDATION=NOT_REACHED\n");
    Text(magic == 0x2BADB002 && bsp && stable && (cr0 & 1U) && !(cr0 & (1U << 31))
         && (!firstGateRejects || exactRejection)
         ? "NATIVE FP FIRMWARE PROBE COMPLETE\n" : "NATIVE FP FIRMWARE PROBE FAIL preconditions\n");
}
