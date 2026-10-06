#include <hardwarecommunication/cpu.h>

using namespace gtos::hardwarecommunication;

namespace {
    uint16_t Read16(const uint8_t* p) { return p[0] | ((uint16_t)p[1] << 8); }
    uint16_t ReadBios16(uint32_t address) {
        // A physical firmware read, not a dereference of a C++ object near null.
        uint16_t value;
        asm volatile("movw (%1), %0" : "=r"(value) : "r"(address) : "memory");
        return value;
    }
    uint32_t Read32(const uint8_t* p) {
        return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
            | ((uint32_t)p[3] << 24);
    }
    bool Equal(const uint8_t* p, const char* signature, uint32_t length) {
        for (uint32_t i = 0; i < length; ++i) if (p[i] != (uint8_t)signature[i]) return false;
        return true;
    }
    bool Checksum(const uint8_t* p, uint32_t length) {
        uint8_t sum = 0;
        for (uint32_t i = 0; i < length; ++i) sum += p[i];
        return sum == 0;
    }
    bool Range(uint32_t address, uint32_t length, uint32_t limit) {
        return address >= 0x400 && address < limit && length <= limit - address;
    }
    bool CpuidAvailable() {
        uint32_t before, after;
        asm volatile("pushfl; popl %0; movl %0, %1; xorl $0x200000, %1;"
                     "pushl %1; popfl; pushfl; popl %1; pushl %0; popfl"
                     : "=&r"(before), "=&r"(after) : : "cc", "memory");
        return ((before ^ after) & 0x200000) != 0;
    }
    void Cpuid(uint32_t leaf, uint32_t subleaf, uint32_t& a, uint32_t& b,
               uint32_t& c, uint32_t& d) {
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(leaf), "c"(subleaf));
    }
    void Store32(char* p, uint32_t value) {
        for (uint32_t i = 0; i < 4; ++i) p[i] = (char)(value >> (8 * i));
    }
    bool AppendUnique(uint32_t* ids, uint32_t& count, uint32_t id) {
        for (uint32_t i = 0; i < count; ++i) if (ids[i] == id) return true;
        if (count == 256) return false;
        ids[count++] = id;
        return true;
    }
    bool ValidSdt(const uint8_t* p, uint32_t available, const char* signature,
                  uint32_t minimum) {
        if (!p || available < minimum || !Equal(p, signature, 4)) return false;
        uint32_t length = Read32(p + 4);
        return length >= minimum && length <= available && length <= 65536
            && Checksum(p, length);
    }
}

CpuManager::CpuManager() {
    uint8_t* bytes = (uint8_t*)&info;
    for (uint32_t i = 0; i < sizeof(info); ++i) bytes[i] = 0;
    const char* unknown = "unknown";
    for (uint32_t i = 0; unknown[i]; ++i) info.vendor[i] = unknown[i];
    info.logicalPerPackage = 1;
    info.detectedLogicalProcessors = info.onlineProcessors = 1;
    info.enumerationSource = CpuBootstrapOnly;
}

void CpuManager::DetectCpuid() {
    info.cpuidAvailable = CpuidAvailable();
    if (!info.cpuidAvailable) return;
    uint32_t a, b, c, d;
    Cpuid(0, 0, a, b, c, d);
    info.maxBasicLeaf = a;
    Store32(info.vendor, b); Store32(info.vendor + 4, d); Store32(info.vendor + 8, c);
    info.vendor[12] = 0;
    Cpuid(0x80000000U, 0, a, b, c, d);
    info.maxExtendedLeaf = a;
    if (info.maxBasicLeaf >= 1) {
        Cpuid(1, 0, a, b, c, d);
        info.featureEcx = c;
        info.featureEdx = d;
        info.bspApicId = b >> 24;
        uint32_t capacity = (b >> 16) & 0xFF;
        if ((d & (1U << 28)) && capacity) info.logicalPerPackage = capacity;
    }
    if (info.maxExtendedLeaf >= 0x80000004U) {
        for (uint32_t i = 0; i < 3; ++i) {
            Cpuid(0x80000002U + i, 0, a, b, c, d);
            Store32(info.brand + i * 16, a); Store32(info.brand + i * 16 + 4, b);
            Store32(info.brand + i * 16 + 8, c); Store32(info.brand + i * 16 + 12, d);
        }
        info.brand[48] = 0;
    }
    // 1F is preferred to 0B. A maximum leaf alone does not prove support:
    // EBX must describe a nonempty level and ECX must identify its level type.
    const uint32_t topologyLeaves[2] = {0x1F, 0x0B};
    for (uint32_t leafIndex = 0; leafIndex < 2; ++leafIndex) {
        uint32_t leaf = topologyLeaves[leafIndex];
        if (info.maxBasicLeaf < leaf) continue;
        uint32_t packageLogical = 0, smt = 0, smtShift = 0;
        for (uint32_t level = 0; level < 32; ++level) {
            Cpuid(leaf, level, a, b, c, d);
            uint32_t count = b & 0xFFFF, type = (c >> 8) & 0xFF;
            if (!count || !type) break;
            info.bspApicId = d;
            if (count > packageLogical) packageLogical = count;
            if (type == 1) { smt = count; smtShift = a & 0x1F; }
        }
        if (packageLogical && smt && packageLogical >= smt && packageLogical % smt == 0) {
            info.logicalPerPackage = packageLogical;
            info.threadsPerCore = smt;
            info.coresPerPackage = packageLogical / smt;
            info.topologyIdsKnown = true;
            info.smtIdShift = (uint8_t)smtShift;
            break;
        }
    }
    // Intel leaf 4 provides a package core capacity. It cannot establish how
    // many CPUs are present system-wide, or safely group sparse APIC IDs.
    if (!info.coresPerPackage && info.maxBasicLeaf >= 4
        && Equal((const uint8_t*)info.vendor, "GenuineIntel", 12)) {
        Cpuid(4, 0, a, b, c, d);
        if (a & 0x1F) {
            info.coresPerPackage = ((a >> 26) & 0x3F) + 1;
            if (info.logicalPerPackage >= info.coresPerPackage
                && info.logicalPerPackage % info.coresPerPackage == 0)
                info.threadsPerCore = info.logicalPerPackage / info.coresPerPackage;
        }
    }
}

void CpuManager::CountPhysicalCores(const uint32_t* ids, uint32_t count) {
    info.detectedPhysicalCores = 0;
    if (!info.topologyIdsKnown) return;
    uint32_t cores[256], coreCount = 0;
    for (uint32_t i = 0; i < count; ++i)
        AppendUnique(cores, coreCount, ids[i] >> info.smtIdShift);
    info.detectedPhysicalCores = coreCount;
}

bool CpuManager::DiscoverFromMadt(const void* table, uint32_t availableBytes) {
    const uint8_t* p = (const uint8_t*)table;
    if (!ValidSdt(p, availableBytes, "APIC", 44)) return false;
    uint32_t length = Read32(p + 4), ids[256], count = 0;
    for (uint32_t offset = 44; offset < length;) {
        if (length - offset < 2) return false;
        uint32_t type = p[offset], entryLength = p[offset + 1];
        if (entryLength < 2 || entryLength > length - offset) return false;
        if (type == 0) {
            if (entryLength < 8) return false;
            if ((Read32(p + offset + 4) & 1)
                && !AppendUnique(ids, count, p[offset + 3])) return false;
        } else if (type == 9) {
            if (entryLength < 16) return false;
            if ((Read32(p + offset + 8) & 1)
                && !AppendUnique(ids, count, Read32(p + offset + 4))) return false;
        }
        offset += entryLength;
    }
    if (!count) return false;
    info.detectedLogicalProcessors = count;
    info.enumerationSource = CpuAcpiMadt;
    info.firmwareTableAddress = 0; // Detect() supplies physical provenance.
    CountPhysicalCores(ids, count);
    return true;
}
bool CpuManager::DiscoverFromMpTable(const void* table, uint32_t availableBytes) {
    const uint8_t* p = (const uint8_t*)table;
    if (!p || availableBytes < 44 || !Equal(p, "PCMP", 4)) return false;
    uint32_t length = Read16(p + 4), entries = Read16(p + 34);
    if (length < 44 || length > availableBytes || (p[6] != 1 && p[6] != 4)
        || !Checksum(p, length)) return false;
    uint32_t ids[256], count = 0, offset = 44;
    for (uint32_t i = 0; i < entries; ++i) {
        if (offset >= length) return false;
        uint32_t type = p[offset];
        if (type > 4) return false;
        uint32_t entryLength = type == 0 ? 20 : 8;
        if (entryLength > length - offset) return false;
        if (type == 0 && (p[offset + 3] & 1)
            && !AppendUnique(ids, count, p[offset + 1])) return false;
        offset += entryLength;
    }
    if (offset != length || !count) return false;
    info.detectedLogicalProcessors = count;
    info.enumerationSource = CpuMpTable;
    info.firmwareTableAddress = 0;
    CountPhysicalCores(ids, count);
    return true;
}

bool CpuManager::TryAcpiRoot(uint32_t address, bool xsdt, uint32_t limit) {
    if (!Range(address, 36, limit)) return false;
    const uint8_t* p = (const uint8_t*)address;
    uint32_t length = Read32(p + 4), stride = xsdt ? 8 : 4;
    if (!Range(address, length, limit) || !ValidSdt(p, length, xsdt ? "XSDT" : "RSDT", 36)
        || (length - 36) % stride) return false;
    for (uint32_t offset = 36; offset < length; offset += stride) {
        if (xsdt && Read32(p + offset + 4)) continue; // Above 4 GiB.
        uint32_t childAddress = Read32(p + offset);
        if (!Range(childAddress, 44, limit)) continue;
        const uint8_t* child = (const uint8_t*)childAddress;
        if (!Equal(child, "APIC", 4)) continue;
        uint32_t childLength = Read32(child + 4);
        if (Range(childAddress, childLength, limit) && DiscoverFromMadt(child, childLength)) {
            info.firmwareTableAddress = childAddress;
            return true;
        }
    }
    return false;
}
bool CpuManager::TryRsdp(uint32_t address, uint32_t limit) {
    if (!Range(address, 20, limit)) return false;
    const uint8_t* p = (const uint8_t*)address;
    if (!Equal(p, "RSD PTR ", 8) || !Checksum(p, 20)) return false;
    if (p[15] >= 2) {
        if (!Range(address, 36, limit)) return false;
        uint32_t length = Read32(p + 20);
        if (length < 36 || length > 4096 || !Range(address, length, limit)
            || !Checksum(p, length)) return false;
        if (!Read32(p + 28) && TryAcpiRoot(Read32(p + 24), true, limit)) return true;
    }
    return TryAcpiRoot(Read32(p + 16), false, limit);
}
bool CpuManager::DetectAcpi(uint32_t limit) {
    if (Range(0x40E, 2, limit)) {
        uint32_t ebda = (uint32_t)ReadBios16(0x40E) << 4;
        if (ebda >= 0x80000 && ebda <= 0x9FC00 && Range(ebda, 1024, limit))
            for (uint32_t offset = 0; offset < 1024; offset += 16)
                if (TryRsdp(ebda + offset, limit)) return true;
    }
    if (Range(0xE0000, 0x20000, limit))
        for (uint32_t address = 0xE0000; address < 0x100000; address += 16)
            if (TryRsdp(address, limit)) return true;
    return false;
}
bool CpuManager::TryMpPointer(uint32_t address, uint32_t limit) {
    if (!Range(address, 16, limit)) return false;
    const uint8_t* p = (const uint8_t*)address;
    if (!Equal(p, "_MP_", 4) || p[8] != 1 || (p[9] != 1 && p[9] != 4)
        || !Checksum(p, 16)) return false;
    uint32_t configAddress = Read32(p + 4);
    // Predefined MP configurations omit a concrete processor list. Keep the
    // conservative BSP fallback instead of inventing processor/APIC identities.
    if (!configAddress || p[11] || !Range(configAddress, 44, limit)) return false;
    const uint8_t* config = (const uint8_t*)configAddress;
    uint32_t length = Read16(config + 4);
    if (!Range(configAddress, length, limit) || !DiscoverFromMpTable(config, length)) return false;
    info.firmwareTableAddress = configAddress;
    return true;
}
bool CpuManager::DetectMp(uint32_t limit) {
    if (Range(0x40E, 7, limit)) {
        uint32_t ebda = (uint32_t)ReadBios16(0x40E) << 4;
        if (ebda >= 0x80000 && ebda <= 0x9FC00 && Range(ebda, 1024, limit))
            for (uint32_t offset = 0; offset < 1024; offset += 16)
                if (TryMpPointer(ebda + offset, limit)) return true;
        uint32_t baseMemory = (uint32_t)ReadBios16(0x413) * 1024;
        if (baseMemory >= 0x80000 && baseMemory <= 0xA0000 && Range(baseMemory - 1024, 1024, limit))
            for (uint32_t address = baseMemory - 1024; address < baseMemory; address += 16)
                if (TryMpPointer(address, limit)) return true;
    }
    if (Range(0xF0000, 0x10000, limit))
        for (uint32_t address = 0xF0000; address < 0x100000; address += 16)
            if (TryMpPointer(address, limit)) return true;
    return false;
}
void CpuManager::Detect(uint32_t accessiblePhysicalBytes) {
    // Reset rather than retaining information from an earlier machine/probe.
    CpuManager empty;
    info = empty.info;
    DetectCpuid();
    if (!DetectAcpi(accessiblePhysicalBytes)) DetectMp(accessiblePhysicalBytes);
}
const CpuInfo& CpuManager::GetInfo() const { return info; }
uint32_t CpuManager::DetectedLogicalProcessors() const { return info.detectedLogicalProcessors; }
uint32_t CpuManager::OnlineProcessors() const { return info.onlineProcessors; }
const char* CpuManager::EnumerationSourceName() const {
    if (info.enumerationSource == CpuAcpiMadt) return "ACPI MADT";
    if (info.enumerationSource == CpuMpTable) return "MP table";
    return "BSP only (firmware topology unavailable)";
}
