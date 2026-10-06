#include <hardwarecommunication/cpu_memory_types.h>

using namespace gtos::hardwarecommunication;

namespace {
    const uint32_t Cr0Protected = 1U, Cr0NotWriteThrough = 1U << 29;
    const uint32_t Cr0CacheDisable = 1U << 30, Cr0Paging = 1U << 31;
    const uint32_t InterruptEnable = 1U << 9;
    const uint32_t FeatureMsr = 1U << 5, FeatureMtrr = 1U << 12;
    const uint32_t FeaturePat = 1U << 16;
    const uint32_t MemoryTypeFeatures = FeatureMsr | FeatureMtrr | FeaturePat;
    const uint32_t FixedMtrrs = 1U << 8;
    const uint32_t WriteCombining = 1U << 10;
    const uint32_t FixedEnable = 1U << 10, MtrrEnable = 1U << 11;
    const uint32_t VariableValid = 1U << 11;
    // VCNT, FIX, WC, SMRR. SMRR itself belongs to firmware/SMM and is not read.
    // Unknown architectural extensions are deliberately rejected, not ignored.
    const uint64_t SupportedMtrrCapabilities = 0xDFFULL;
    const uint32_t FixedMsrs[CpuMemoryTypes::FixedMtrrCount] = {
        0x250, 0x258, 0x259, 0x268, 0x269, 0x26A,
        0x26B, 0x26C, 0x26D, 0x26E, 0x26F
    };
    static_assert(sizeof(CpuMemoryTypes) == 640, "fixed memory-type snapshot ABI");

    uint32_t Control0() {
        uint32_t value;
        asm volatile("movl %%cr0,%0" : "=r"(value) : : "memory");
        return value;
    }
    uint32_t Flags() {
        uint32_t value;
        asm volatile("pushfl; popl %0" : "=r"(value) : : "memory");
        return value;
    }
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
    }
    uint64_t ReadMsr(uint32_t index) {
        uint32_t low, high;
        asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(index) : "memory");
        return ((uint64_t)high << 32) | low;
    }
    void Clear(CpuMemoryTypes& result) {
        // Volatile stores keep this freestanding even without -fno-builtin.
        volatile uint8_t* bytes = (volatile uint8_t*)&result;
        for (uint32_t i = 0; i < sizeof(result); ++i) bytes[i] = 0;
    }
    bool ValidMtrrType(uint32_t type, uint64_t capability) {
        return type == 0 || type == 4 || type == 5 || type == 6
            || (type == 1 && (capability & WriteCombining));
    }
    uint64_t AddressMask(const CpuMemoryTypes& types) {
        // Call only after checking physicalAddressBits, avoiding invalid shifts.
        return ((1ULL << types.physicalAddressBits) - 1) & ~0xFFFULL;
    }
    bool ValidConfiguration(const CpuMemoryTypes& types) {
        if (types.cpuidFeatures & ~MemoryTypeFeatures
            || types.physicalAddressBits < 32 || types.physicalAddressBits > 52
            || ((types.cpuidFeatures & (FeatureMtrr | FeaturePat))
                && !(types.cpuidFeatures & FeatureMsr))) return false;
        if (types.cpuidFeatures & FeaturePat) {
            for (uint32_t i = 0; i < 8; ++i) {
                const uint32_t entry = (uint32_t)(types.pat >> (i * 8)) & 0xFF;
                if (!(entry == 0 || entry == 1 || entry == 4 || entry == 5
                      || entry == 6 || entry == 7)) return false;
            }
            // Shared normal RAM uses PAT/PWT/PCD=000. Device mappings use 011;
            // strong UC is required there, not UC- (which MTRR WC can override).
            if ((types.pat & 0xFF) != 6 || ((types.pat >> 24) & 0xFF) != 0)
                return false;
        } else if (types.pat != 0) return false;
        if (!(types.cpuidFeatures & FeatureMtrr))
            return !types.capability && !types.defaultType && !types.variableCount;
        if ((types.capability & ~SupportedMtrrCapabilities)
            || types.variableCount > CpuMemoryTypes::MaximumVariableMtrrs
            || types.variableCount != (types.capability & 0xFF)
            || (types.defaultType & ~0xCFFULL)
            || !ValidMtrrType((uint32_t)types.defaultType & 0xFF, types.capability)
            || ((types.defaultType & FixedEnable) && !(types.capability & FixedMtrrs)))
            return false;
        if (types.capability & FixedMtrrs)
            for (uint32_t i = 0; i < CpuMemoryTypes::FixedMtrrCount; ++i)
                for (uint32_t byte = 0; byte < 8; ++byte)
                    if (!ValidMtrrType((uint32_t)(types.fixed[i] >> (byte * 8)) & 0xFF,
                                       types.capability)) return false;
        const uint64_t addressMask = AddressMask(types);
        for (uint32_t i = 0; i < types.variableCount; ++i) {
            const uint64_t base = types.variableBase[i], mask = types.variableMask[i];
            if ((base & ~(addressMask | 0xFFULL))
                || (mask & ~(addressMask | VariableValid))
                || !ValidMtrrType((uint32_t)base & 0xFF, types.capability)) return false;
            if (mask & VariableValid) {
                // A supported active mask must describe one aligned power-of-2
                // range. Do not interpret sparse masks or silently align a base.
                const uint64_t sizeMinusOne = (addressMask & ~mask) | 0xFFFULL;
                if ((sizeMinusOne & (sizeMinusOne + 1))
                    || ((base & addressMask) & sizeMinusOne)) return false;
            }
        }
        return true;
    }
    uint32_t MemoryTypeAt(const CpuMemoryTypes& types, uint64_t address) {
        if (!(types.cpuidFeatures & FeatureMtrr)) return 6;
        if (!(types.defaultType & MtrrEnable)) return 0;
        if (address < 0x100000 && (types.capability & FixedMtrrs)
            && (types.defaultType & FixedEnable)) {
            uint32_t index, byte;
            if (address < 0x80000) { index = 0; byte = (uint32_t)address >> 16; }
            else if (address < 0xC0000) {
                index = 1 + ((uint32_t)(address - 0x80000) >> 17);
                byte = ((uint32_t)address >> 14) & 7;
            } else {
                index = 3 + ((uint32_t)(address - 0xC0000) >> 15);
                byte = ((uint32_t)address >> 12) & 7;
            }
            return (uint32_t)(types.fixed[index] >> (byte * 8)) & 0xFF;
        }
        uint32_t matches = 0;
        const uint64_t addressMask = AddressMask(types);
        for (uint32_t i = 0; i < types.variableCount; ++i) {
            const uint64_t mask = types.variableMask[i] & addressMask;
            if ((types.variableMask[i] & VariableValid)
                && (address & mask) == (types.variableBase[i] & mask))
                matches |= 1U << ((uint32_t)types.variableBase[i] & 0xFF);
        }
        // Resolve all matches together: UC also dominates an otherwise
        // undefined combination, regardless of register enumeration order.
        if (!matches) return (uint32_t)types.defaultType & 0xFF;
        if (matches & 1U) return 0;
        if (matches == (1U << 1)) return 1;
        if (matches == (1U << 4) || matches == ((1U << 4) | (1U << 6))) return 4;
        if (matches == (1U << 5)) return 5;
        if (matches == (1U << 6)) return 6;
        return 0xFF; // Architectural behavior for other overlaps is undefined.
    }
    bool ReadMemoryTypes(CpuMemoryTypes& result) {
        Clear(result);
        if (!CpuidAvailable()) return false;
        uint32_t a, b, c, d;
        Cpuid(0, a, b, c, d);
        if (a < 1) return false;
        Cpuid(1, a, b, c, d);
        result.cpuidFeatures = d & MemoryTypeFeatures;
        // These guards precede *every* possible RDMSR path. Both optional
        // features may be absent, but an advertised MSR needs MSR support.
        if ((result.cpuidFeatures & (FeatureMtrr | FeaturePat))
            && !(result.cpuidFeatures & FeatureMsr)) return false;

        // Intel SDM 12.11.2.3 permits 36 bits when 80000008h is unavailable.
        result.physicalAddressBits = 36;
        Cpuid(0x80000000U, a, b, c, d);
        if (a >= 0x80000008U) {
            Cpuid(0x80000008U, a, b, c, d);
            result.physicalAddressBits = a & 0xFF;
        }
        if (result.physicalAddressBits < 32 || result.physicalAddressBits > 52)
            return false;
        if (result.cpuidFeatures & FeatureMtrr) {
            result.capability = ReadMsr(0xFE); // IA32_MTRRCAP
            result.variableCount = (uint32_t)result.capability & 0xFF;
            if ((result.capability & ~SupportedMtrrCapabilities)
                || result.variableCount > CpuMemoryTypes::MaximumVariableMtrrs)
                return false;
            result.defaultType = ReadMsr(0x2FF); // IA32_MTRR_DEF_TYPE
            for (uint32_t i = 0; i < result.variableCount; ++i) {
                result.variableBase[i] = ReadMsr(0x200 + i * 2);
                result.variableMask[i] = ReadMsr(0x201 + i * 2);
            }
            // Read supported registers even if their ranges are disabled.
            if (result.capability & FixedMtrrs)
                for (uint32_t i = 0; i < CpuMemoryTypes::FixedMtrrCount; ++i)
                    result.fixed[i] = ReadMsr(FixedMsrs[i]);
        }
        if (result.cpuidFeatures & FeaturePat) result.pat = ReadMsr(0x277);
        if (!ValidConfiguration(result)) return false;
        result.valid = 1;
        return true;
    }
    bool Equivalent(const CpuMemoryTypes& a, const CpuMemoryTypes& b) {
        if (a.valid != 1 || b.valid != 1 || a.cpuidFeatures != b.cpuidFeatures
            || a.physicalAddressBits != b.physicalAddressBits
            || a.capability != b.capability || a.variableCount != b.variableCount
            || a.defaultType != b.defaultType || a.pat != b.pat) return false;
        // b was captured with a checked bound, and counts now match.
        for (uint32_t i = 0; i < b.variableCount; ++i)
            if (a.variableBase[i] != b.variableBase[i]
                || a.variableMask[i] != b.variableMask[i]) return false;
        if (b.capability & FixedMtrrs)
            for (uint32_t i = 0; i < CpuMemoryTypes::FixedMtrrCount; ++i)
                if (a.fixed[i] != b.fixed[i]) return false;
        return true;
    }
}

bool gtos::hardwarecommunication::CaptureMemoryTypes(CpuMemoryTypes& result) {
    result.valid = 0;
    const uint32_t cr0 = Control0();
    if (!(cr0 & Cr0Protected)
        || (cr0 & (Cr0Paging | Cr0CacheDisable | Cr0NotWriteThrough))) return false;
    return ReadMemoryTypes(result);
}

bool gtos::hardwarecommunication::MemoryTypeRangeIsWriteBack(
    const CpuMemoryTypes& types, uint32_t physicalAddress, uint32_t length) {
    const uint64_t end = (uint64_t)physicalAddress + length;
    if (!length || end > (1ULL << 32) || types.valid != 1
        || !ValidConfiguration(types)) return false;
    // Every MTRR boundary is at least 4 KiB aligned. Inspecting the start of
    // every touched granule therefore covers unaligned/partial byte ranges too.
    for (uint64_t page = physicalAddress & ~0xFFFU; page < end; page += 4096)
        if (MemoryTypeAt(types, page) != 6) return false;
    return true;
}

bool gtos::hardwarecommunication::PrepareApplicationProcessorMemory(
    const CpuMemoryTypes& bootstrap) {
    const uint32_t cr0 = Control0();
    if (bootstrap.valid != 1 || !(cr0 & Cr0Protected)
        || (cr0 & Cr0Paging) || (Flags() & InterruptEnable)) return false;
    CpuMemoryTypes application;
    if (!ReadMemoryTypes(application) || !Equivalent(bootstrap, application)) return false;

    // Intel SDM Vol. 3A 12.5.1, 12.5.3 and 12.11.8: normalize INIT's CD/NW
    // state to CD=1/NW=0, write back/invalidate, then enter normal cache mode.
    // No MTRR/PAT writes take place, so this is NOT the global MTRR-update
    // algorithm. PG is already zero and remains zero; no stale translations
    // are used and the caller installs its shared CR3 before enabling paging.
    const uint32_t noFill = (cr0 | Cr0CacheDisable) & ~Cr0NotWriteThrough;
    const uint32_t enabled = cr0 & ~(Cr0CacheDisable | Cr0NotWriteThrough);
    asm volatile("movl %0,%%cr0; wbinvd; movl %1,%%cr0"
                 : : "r"(noFill), "r"(enabled) : "memory");
    return Control0() == enabled;
}
