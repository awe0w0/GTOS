#include <hardwarecommunication/cpu_startup.h>

using namespace gtos;
using namespace gtos::hardwarecommunication;

extern "C" {
    extern uint8_t gtos_ap_trampoline_start[], gtos_ap_trampoline_end[];
    extern uint8_t gtos_ap_trampoline_gdt[], gtos_ap_trampoline_gdtr[];
    extern uint8_t gtos_ap_trampoline_jump[], gtos_ap_trampoline_parameters[];
    extern uint8_t gtos_ap_trampoline_protected[];
    void gtos_ap_fault_halt();
}

namespace {
    const uint32_t StackPages = 4;
    const uint32_t MaximumProcessors = 256;
    bool anyStartupAttempted = false;
    struct ApRecord {
        uint32_t apicId;
        volatile uint32_t state;
        uint32_t stackBase;
        uint32_t stackPages;
        volatile uint32_t observedApicId;
        volatile uint32_t observedStackPointer;
        volatile uint32_t checksum;
        uint32_t expectedChecksum; // BSP-owned, immutable before any IPI.
        uint32_t reserved[8];
    } __attribute__((aligned(64)));
    struct ApParameters {
        uint32_t entry;
        uint32_t idtr;
        uint32_t stackTops[256];
        uint32_t records[256];
    };
    struct ApGate {
        uint16_t addressLow, selector;
        uint8_t reserved, access;
        uint16_t addressHigh;
    } __attribute__((packed));
    struct ApIdtr { uint16_t limit; uint32_t base; } __attribute__((packed));
    static_assert(sizeof(ApRecord) == 64, "per-AP cache-line record");
    static_assert(__builtin_offsetof(ApParameters, stackTops) == 8, "trampoline stack table ABI");
    static_assert(__builtin_offsetof(ApParameters, records) == 1032, "trampoline record table ABI");

    uint8_t In8(uint16_t port) {
        uint8_t value; asm volatile("inb %1,%0" : "=a"(value) : "Nd"(port)); return value;
    }
    void Out8(uint16_t port, uint8_t value) { asm volatile("outb %0,%1" : : "a"(value), "Nd"(port)); }
    uint16_t Read16(const uint8_t* p) { return p[0] | ((uint16_t)p[1] << 8); }
    uint32_t Read32(const uint8_t* p) {
        return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    void Write32(uint8_t* p, uint32_t value) {
        for (uint32_t i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
    }
    bool PhysicalRange(uint32_t address, uint32_t length, uint32_t limit) {
        return address >= 0x400 && address < limit && length <= limit - address;
    }
    void Zero(void* address, uint32_t length) {
        uint8_t* p = (uint8_t*)address;
        for (uint32_t i = 0; i < length; ++i) p[i] = 0;
    }
    bool AddUnique(uint32_t* ids, uint32_t capacity, uint32_t& count, uint32_t id) {
        for (uint32_t i = 0; i < count; ++i) if (ids[i] == id) return true;
        if (count == capacity) return false;
        ids[count++] = id;
        return true;
    }
    uint32_t Flags() { uint32_t flags; asm volatile("pushfl; popl %0" : "=r"(flags)); return flags; }
    uint32_t Control0() { uint32_t value; asm volatile("movl %%cr0,%0" : "=r"(value)); return value; }
    uint32_t ApicId() {
        uint32_t a, b, c, d;
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
        return b >> 24;
    }
    uint32_t LoadState(const ApRecord& record) {
        return __atomic_load_n(&record.state, __ATOMIC_ACQUIRE);
    }
    void StoreState(ApRecord& record, CpuStartupState state) {
        __atomic_store_n(&record.state, (uint32_t)state, __ATOMIC_RELEASE);
    }
    uint32_t LoadObservation(const volatile uint32_t& value) {
        return __atomic_load_n(&value, __ATOMIC_RELAXED);
    }
    void StoreObservation(volatile uint32_t& target, uint32_t value) {
        __atomic_store_n(&target, value, __ATOMIC_RELAXED);
    }
    void DeliveryFailedIfStarting(ApRecord& record) {
        uint32_t expected = CpuStartupStarting;
        __atomic_compare_exchange_n(&record.state, &expected, (uint32_t)CpuStartupDeliveryFailed,
                                     false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    }
    bool IdentityPublished(uint32_t state) {
        return state == CpuStartupEntered || state == CpuStartupParked
            || state == CpuStartupSelfTestFailed;
    }
    uint32_t VerifiedState(const ApRecord& record) {
        uint32_t state = LoadState(record);
        if (state != CpuStartupParked) return state;
        // Recheck every terminal observation, including an AP arriving after
        // Start() timed out. AP-published Parked alone is not BSP verification.
        uint32_t observedSp = LoadObservation(record.observedStackPointer);
        uint64_t stackEnd = (uint64_t)record.stackBase + (uint64_t)record.stackPages * 4096;
        if (LoadObservation(record.observedApicId) != record.apicId
            || !record.stackPages || observedSp < record.stackBase || (uint64_t)observedSp >= stackEnd
            || LoadObservation(record.checksum) != record.expectedChecksum)
            return CpuStartupSelfTestFailed;
        return state;
    }
    uint32_t SelfTestChecksum(uint32_t id) {
        uint32_t value = 0x47544F53U ^ id;
        for (uint32_t i = 0; i < 4096; ++i)
            value = ((value << 5) | (value >> 27)) ^ (i * 2654435761U + id);
        return value;
    }
    // Early boot owns otherwise-unused PIT channel 2. Preserve the speaker/gate
    // bits, leave PIT channel0 untouched and use finite polls for absent hardware.
    bool DelayMicroseconds(uint32_t microseconds) {
        if (!microseconds || microseconds > 50000) return false;
        uint32_t count = (microseconds * 1194U + 999) / 1000;
        uint8_t saved = In8(0x61);
        Out8(0x61, saved & (uint8_t)~3U);
        Out8(0x43, 0xB0); // channel2, low/high bytes, one-shot mode0, binary.
        Out8(0x42, (uint8_t)count); Out8(0x42, (uint8_t)(count >> 8));
        Out8(0x61, (saved & (uint8_t)~2U) | 1);
        bool done = false;
        for (uint32_t poll = 0; poll < 10000000U; ++poll) {
            if (In8(0x61) & 0x20) { done = true; break; }
            asm volatile("pause");
        }
        Out8(0x61, saved);
        return done;
    }
}
namespace gtos { namespace hardwarecommunication {
    struct CpuStartupShared {
        ApParameters parameters;
        ApIdtr idtr;
        ApGate idt[256];
        ApRecord processors[MaximumProcessors];
    };
} }

CpuStartup::CpuStartup() : shared(0), used(false) {
    Zero(&report, sizeof(report));
    report.schedulerOnlineProcessors = 1;
}

bool CpuStartup::ReadFirmwareInventory(const CpuInfo& cpu, uint32_t limit,
                                       uint32_t* ids, uint32_t capacity,
                                       uint32_t& count, uint32_t& lapicAddress) {
    count = 0; lapicAddress = 0;
    if (!ids || !capacity || capacity > MaximumProcessors
        || !PhysicalRange(cpu.firmwareTableAddress, 44, limit)) return false;
    const uint8_t* table = (const uint8_t*)cpu.firmwareTableAddress;
    CpuManager validator;
    uint32_t length;
    if (cpu.enumerationSource == CpuAcpiMadt) {
        length = Read32(table + 4);
        if (!PhysicalRange(cpu.firmwareTableAddress, length, limit)
            || !validator.DiscoverFromMadt(table, length)) return false;
        lapicAddress = Read32(table + 36);
        bool overrideSeen = false;
        for (uint32_t offset = 44; offset < length; offset += table[offset + 1]) {
            uint32_t type = table[offset], bytes = table[offset + 1];
            if (type == 0 && (Read32(table + offset + 4) & 1)) {
                if (!AddUnique(ids, capacity, count, table[offset + 3])) return false;
            } else if (type == 9 && (Read32(table + offset + 8) & 1)) {
                if (!AddUnique(ids, capacity, count, Read32(table + offset + 4))) return false;
            } else if (type == 5) {
                if (bytes < 12 || overrideSeen || Read32(table + offset + 8)) return false;
                lapicAddress = Read32(table + offset + 4); overrideSeen = true;
            }
        }
    } else if (cpu.enumerationSource == CpuMpTable) {
        length = Read16(table + 4);
        if (!PhysicalRange(cpu.firmwareTableAddress, length, limit)
            || !validator.DiscoverFromMpTable(table, length)) return false;
        lapicAddress = Read32(table + 36);
        for (uint32_t offset = 44; offset < length; offset += table[offset] == 0 ? 20 : 8)
            if (table[offset] == 0 && (table[offset + 3] & 1))
                if (!AddUnique(ids, capacity, count, table[offset + 1])) return false;
    } else return false;
    if (!lapicAddress || (lapicAddress & 4095) || count != cpu.detectedLogicalProcessors) return false;
    for (uint32_t i = 0; i < count; ++i) if (ids[i] == cpu.bspApicId) return true;
    return false;
}

bool CpuStartup::InitializeTrampoline(uint32_t address) {
    uint32_t size = (uint32_t)gtos_ap_trampoline_end - (uint32_t)gtos_ap_trampoline_start;
    if (!shared || address < 0x10000 || address >= 0xA0000 || (address & 4095) || size > 4096) return false;
    uint8_t* destination = (uint8_t*)address;
    Zero(destination, 4096);
    for (uint32_t i = 0; i < size; ++i) destination[i] = gtos_ap_trampoline_start[i];
    uint32_t gdtrOffset = (uint32_t)gtos_ap_trampoline_gdtr - (uint32_t)gtos_ap_trampoline_start;
    uint32_t gdtOffset = (uint32_t)gtos_ap_trampoline_gdt - (uint32_t)gtos_ap_trampoline_start;
    uint32_t jumpOffset = (uint32_t)gtos_ap_trampoline_jump - (uint32_t)gtos_ap_trampoline_start;
    uint32_t protectedOffset = (uint32_t)gtos_ap_trampoline_protected - (uint32_t)gtos_ap_trampoline_start;
    uint32_t parametersOffset = (uint32_t)gtos_ap_trampoline_parameters - (uint32_t)gtos_ap_trampoline_start;
    Write32(destination + gdtrOffset + 2, address + gdtOffset);
    Write32(destination + jumpOffset, address + protectedOffset);
    Write32(destination + parametersOffset, (uint32_t)&shared->parameters);
    asm volatile("" : : : "memory");
    return true;
}
bool CpuStartup::WaitDelivery() {
    volatile uint32_t* lapic = (volatile uint32_t*)report.localApicAddress;
    for (uint32_t i = 0; i < 1000; ++i) {
        if (!(lapic[0x300 / 4] & (1U << 12))) return true;
        if (!DelayMicroseconds(10)) return false;
    }
    return false;
}
bool CpuStartup::SendIpi(uint32_t apicId, uint32_t command) {
    if (apicId >= 255 || !WaitDelivery()) return false;
    volatile uint32_t* lapic = (volatile uint32_t*)report.localApicAddress;
    lapic[0x310 / 4] = apicId << 24;
    lapic[0x300 / 4] = command;
    (void)lapic[0x20 / 4]; // Drain the posted MMIO write.
    return WaitDelivery();
}

void CpuStartup::ApplicationProcessorEntry(void* opaque) {
    ApRecord& record = *(ApRecord*)opaque;
    uint32_t sp;
    asm volatile("movl %%esp,%0" : "=r"(sp));
    uint32_t identity = ApicId();
    StoreObservation(record.observedApicId, identity);
    StoreObservation(record.observedStackPointer, sp);
    StoreState(record, CpuStartupEntered); // Publishes identity and stack observation.
    StoreObservation(record.checksum, SelfTestChecksum(identity));
    bool passed = identity == record.apicId
        && sp >= record.stackBase && sp < record.stackBase + record.stackPages * 4096
        && !(Flags() & 0x200) && (Control0() & 0x80000001U) == 1;
    StoreState(record, passed ? CpuStartupParked : CpuStartupSelfTestFailed);
    for (;;) asm volatile("cli; hlt" : : : "memory");
}

bool CpuStartup::Start(const CpuInfo& cpu, memory::PhysicalMemoryManager& frames, uint32_t limit) {
    if (used || anyStartupAttempted) { report.error = CpuStartupAlreadyRun; return false; }
    used = true;
    if (Flags() & 0x200) { report.error = CpuStartupInterruptsEnabled; return false; }
    if (Control0() & 0x80000000U) { report.error = CpuStartupPagingEnabled; return false; }
    if (cpu.enumerationSource == CpuBootstrapOnly) { report.error = CpuStartupNoFirmware; return false; }
    uint32_t ids[MaximumProcessors], count = 0, firmwareLapic = 0;
    if (!ReadFirmwareInventory(cpu, limit, ids, MaximumProcessors, count, firmwareLapic)) {
        report.error = CpuStartupInvalidFirmware; return false;
    }
    report.detectedProcessors = count;
    if (count <= 1) { report.error = CpuStartupOk; return true; }
    if (!cpu.cpuidAvailable || (cpu.featureEdx & ((1U << 5) | (1U << 9))) != ((1U << 5) | (1U << 9))) {
        report.error = CpuStartupNoLocalApic; return false;
    }
    uint32_t baseLow, baseHigh;
    asm volatile("rdmsr" : "=a"(baseLow), "=d"(baseHigh) : "c"(0x1B));
    if (baseLow & (1U << 10)) { report.error = CpuStartupX2ApicUnsupported; return false; }
    uint32_t base = baseLow & 0xFFFFF000U;
    if (baseHigh || (baseLow & 0x900) != 0x900 || base != firmwareLapic || base < limit
        || base < 0xF0000000U || ApicId() != cpu.bspApicId) {
        report.error = CpuStartupBadApicBase; return false;
    }
    report.localApicAddress = base;
    volatile uint32_t* lapic = (volatile uint32_t*)base;
    uint32_t version = lapic[0x30 / 4];
    if (version == 0xFFFFFFFFU || (version & 0xFF) < 0x10
        || (lapic[0x20 / 4] >> 24) != cpu.bspApicId) {
        report.error = CpuStartupBadApicBase; return false;
    }
    if (!DelayMicroseconds(100)) { report.error = CpuStartupTimerUnavailable; return false; }
    if (!frames.claimLowBootstrapPage(report.trampolineAddress)) {
        report.error = CpuStartupNoTrampoline; return false;
    }
    uint32_t sharedAddress = 0;
    const uint32_t sharedPages = (sizeof(CpuStartupShared) + 4095) / 4096;
    if (!frames.allocateContiguous(sharedPages, sharedAddress)) {
        report.error = CpuStartupNoSharedMemory; return false;
    }
    shared = (CpuStartupShared*)sharedAddress;
    Zero(shared, sharedPages * 4096);
    shared->parameters.entry = (uint32_t)&ApplicationProcessorEntry;
    shared->parameters.idtr = (uint32_t)&shared->idtr;
    shared->idtr.limit = sizeof(shared->idt) - 1;
    shared->idtr.base = (uint32_t)shared->idt;
    for (uint32_t i = 0; i < 256; ++i) {
        shared->idt[i].addressLow = (uint32_t)&gtos_ap_fault_halt & 0xFFFF;
        shared->idt[i].addressHigh = (uint32_t)&gtos_ap_fault_halt >> 16;
        shared->idt[i].selector = 0x08;
        shared->idt[i].access = 0x8E;
    }
    for (uint32_t i = 0; i < count; ++i) {
        ApRecord& record = shared->processors[i];
        record.apicId = ids[i]; StoreObservation(record.observedApicId, 0xFFFFFFFFU);
        if (ids[i] == cpu.bspApicId) { StoreState(record, CpuStartupBootstrap); continue; }
        if (ids[i] >= 255) { StoreState(record, CpuStartupUnsupportedId); continue; }
        if (!frames.allocateContiguous(StackPages, record.stackBase, 1, 0xFFFFEFFFU)) { StoreState(record, CpuStartupNoStack); continue; }
        record.stackPages = StackPages;
        record.expectedChecksum = SelfTestChecksum(record.apicId);
        shared->parameters.stackTops[ids[i]] = record.stackBase + StackPages * 4096;
        shared->parameters.records[ids[i]] = (uint32_t)&record;
    }
    if (!InitializeTrampoline(report.trampolineAddress)) {
        report.error = CpuStartupBadTrampoline;
        // Nothing was sent, so reclaim ordinary shared/stack allocations safely.
        for (uint32_t i = 0; i < count; ++i)
            if (shared->processors[i].stackPages)
                frames.freeContiguous(shared->processors[i].stackBase, shared->processors[i].stackPages);
        frames.freeContiguous(sharedAddress, sharedPages); shared = 0;
        return false;
    }
    anyStartupAttempted = true;
    const uint32_t savedSvr = lapic[0xF0 / 4];
    lapic[0xF0 / 4] = (savedSvr & ~0xFFU) | 0x1FFU;
    (void)lapic[0x20 / 4];
    for (uint32_t i = 0; i < count; ++i) {
        ApRecord& record = shared->processors[i];
        if (!record.stackPages) continue;
        ++report.attemptedAps;
        StoreState(record, CpuStartupStarting);
        // Physical destination, level-assert INIT then deassert before SIPI.
        if (!SendIpi(record.apicId, 0xC500) || !DelayMicroseconds(200)
            || !SendIpi(record.apicId, 0x8500) || !DelayMicroseconds(10000)
            || !SendIpi(record.apicId, 0x4600 | (report.trampolineAddress >> 12))
            || !DelayMicroseconds(200)) {
            DeliveryFailedIfStarting(record); continue;
        }
        if (LoadState(record) == CpuStartupStarting
            && !SendIpi(record.apicId, 0x4600 | (report.trampolineAddress >> 12))) {
            DeliveryFailedIfStarting(record); continue;
        }
        bool timerOk = true;
        for (uint32_t wait = 0; wait < 1000; ++wait) {
            uint32_t state = LoadState(record);
            if (state == CpuStartupParked || state == CpuStartupSelfTestFailed) break;
            if (!DelayMicroseconds(100)) { timerOk = false; break; }
        }
        uint32_t state = LoadState(record);
        if (state != CpuStartupParked && state != CpuStartupSelfTestFailed) {
            // CAS avoids overwriting a simultaneously published successful ACK.
            uint32_t expected = state;
            __atomic_compare_exchange_n(&record.state, &expected, (uint32_t)CpuStartupTimedOut,
                                         false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        }
        if (!timerOk) report.error = CpuStartupTimerUnavailable;
        if (LoadState(record) == CpuStartupParked && VerifiedState(record) != CpuStartupParked)
            StoreState(record, CpuStartupSelfTestFailed);
    }
    lapic[0xF0 / 4] = savedSvr; (void)lapic[0x20 / 4];
    CpuStartupReport final = GetReport();
    if (report.error == CpuStartupOk && final.parkedAps != count - 1) report.error = CpuStartupPartialFailure;
    return report.error == CpuStartupOk;
}

CpuStartupReport CpuStartup::GetReport() const {
    CpuStartupReport result = report;
    result.acknowledgedAps = result.parkedAps = result.failedAps = 0;
    if (!shared) return result;
    for (uint32_t i = 0; i < report.detectedProcessors; ++i) {
        const ApRecord& record = shared->processors[i];
        uint32_t state = VerifiedState(record);
        if (state == CpuStartupBootstrap) continue;
        if (IdentityPublished(state) && LoadObservation(record.observedApicId) == record.apicId)
            ++result.acknowledgedAps;
        if (state == CpuStartupParked) ++result.parkedAps;
        else if (state != CpuStartupUnattempted && state != CpuStartupStarting && state != CpuStartupEntered)
            ++result.failedAps;
    }
    if (result.failedAps && result.error == CpuStartupOk) result.error = CpuStartupPartialFailure;
    return result;
}
bool CpuStartup::GetProcessor(uint32_t index, CpuStartupProcessorInfo& result) const {
    if (!shared || index >= report.detectedProcessors) return false;
    const ApRecord& record = shared->processors[index];
    result.state = (CpuStartupState)VerifiedState(record);
    result.apicId = record.apicId;
    result.stackBase = record.stackBase; result.stackPages = record.stackPages;
    bool published = IdentityPublished(result.state);
    result.observedApicId = published ? LoadObservation(record.observedApicId) : 0xFFFFFFFFU;
    result.observedStackPointer = published ? LoadObservation(record.observedStackPointer) : 0;
    result.selfTestChecksum = result.state == CpuStartupParked || result.state == CpuStartupSelfTestFailed
        ? LoadObservation(record.checksum) : 0;
    return true;
}
const char* CpuStartup::ErrorName(CpuStartupError error) {
    switch (error) {
        case CpuStartupOk: return "AP startup/self-test complete";
        case CpuStartupAlreadyRun: return "AP startup already attempted";
        case CpuStartupInterruptsEnabled: return "AP startup requires interrupts disabled";
        case CpuStartupPagingEnabled: return "AP startup requires pre-paging identity access";
        case CpuStartupNoFirmware: return "no firmware processor inventory";
        case CpuStartupInvalidFirmware: return "invalid or inconsistent firmware processor inventory";
        case CpuStartupNoLocalApic: return "local APIC/MSR support unavailable";
        case CpuStartupX2ApicUnsupported: return "x2APIC mode startup not implemented";
        case CpuStartupBadApicBase: return "local APIC address/identity validation failed";
        case CpuStartupNoTrampoline: return "no safe low-memory trampoline page";
        case CpuStartupBadTrampoline: return "invalid trampoline image or location";
        case CpuStartupNoSharedMemory: return "no memory for per-AP startup records";
        case CpuStartupTimerUnavailable: return "PIT channel2 delay unavailable";
        case CpuStartupPartialFailure: return "one or more APs failed startup/self-test";
    }
    return "unknown AP startup error";
}

#ifdef GTOS_CPU_STARTUP_TEST
// Exercise late publication and corrupted observations without privileged I/O.
extern "C" bool GtosApObservationSelfTest() {
    ApRecord record;
    Zero(&record, sizeof(record));
    record.apicId = 7; record.stackBase = 0xFFFFB000U; record.stackPages = 4;
    record.expectedChecksum = SelfTestChecksum(record.apicId);
    StoreObservation(record.observedApicId, record.apicId);
    StoreObservation(record.observedStackPointer, 0xFFFFEFE0U);
    StoreObservation(record.checksum, record.expectedChecksum);
    StoreState(record, CpuStartupParked);
    if (VerifiedState(record) != CpuStartupParked) return false;
    StoreObservation(record.checksum, record.expectedChecksum ^ 1);
    if (VerifiedState(record) != CpuStartupSelfTestFailed) return false;
    StoreState(record, CpuStartupTimedOut);
    if (VerifiedState(record) != CpuStartupTimedOut) return false;
    StoreState(record, CpuStartupParked); // Late invalid ACK cannot bypass verification.
    if (VerifiedState(record) != CpuStartupSelfTestFailed) return false;
    StoreObservation(record.checksum, record.expectedChecksum);
    if (VerifiedState(record) != CpuStartupParked) return false;
    StoreObservation(record.observedStackPointer, 0xFFFFF000U);
    if (VerifiedState(record) != CpuStartupSelfTestFailed) return false;
    StoreObservation(record.observedStackPointer, 0xFFFFEFE0U);
    StoreObservation(record.observedApicId, 8);
    return VerifiedState(record) == CpuStartupSelfTestFailed;
}
#endif
