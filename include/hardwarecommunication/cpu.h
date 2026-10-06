#ifndef __GTOS__HARDWARECOMMUNICATION__CPU_H
#define __GTOS__HARDWARECOMMUNICATION__CPU_H

#include <common/types.h>

namespace gtos { namespace hardwarecommunication {
    enum CpuEnumerationSource { CpuBootstrapOnly, CpuAcpiMadt, CpuMpTable };

    struct CpuInfo {
        char vendor[13];
        char brand[49];
        bool cpuidAvailable;
        uint32_t maxBasicLeaf;
        uint32_t maxExtendedLeaf;
        uint32_t featureEcx;
        uint32_t featureEdx;
        uint32_t bspApicId;
        // CPUID capacities for the boot processor's package, not system totals.
        uint32_t logicalPerPackage;
        uint32_t coresPerPackage; // 0 means unknown.
        uint32_t threadsPerCore; // 0 means unknown.
        // Counts firmware-enabled logical CPUs. Hotplug-only entries excluded.
        uint32_t detectedLogicalProcessors;
        // 0 unless CPUID topology levels allow firmware APIC IDs to be grouped.
        uint32_t detectedPhysicalCores;
        uint32_t onlineProcessors; // 1: only the BSP has actually been started.
        CpuEnumerationSource enumerationSource;
        uint32_t firmwareTableAddress;
        bool topologyIdsKnown;
        uint8_t smtIdShift;
    };

    class CpuManager {
        CpuInfo info;
        void DetectCpuid();
        void CountPhysicalCores(const uint32_t* ids, uint32_t count);
        bool DetectAcpi(uint32_t accessiblePhysicalBytes);
        bool DetectMp(uint32_t accessiblePhysicalBytes);
        bool TryAcpiRoot(uint32_t address, bool xsdt, uint32_t accessiblePhysicalBytes);
        bool TryRsdp(uint32_t address, uint32_t accessiblePhysicalBytes);
        bool TryMpPointer(uint32_t address, uint32_t accessiblePhysicalBytes);
    public:
        CpuManager();
        // Boot-time, identity-mapped physical memory only. The caller supplies
        // the smaller of its accessible segment/mapping limit and physical RAM
        // extent; out-of-range firmware pointers are skipped, never followed.
        void Detect(uint32_t accessiblePhysicalBytes = 64U * 1024U * 1024U);
        const CpuInfo& GetInfo() const;
        uint32_t DetectedLogicalProcessors() const;
        uint32_t OnlineProcessors() const;
        const char* EnumerationSourceName() const;
        // Pure bounded table parsers also usable by a future bootloader/mapper.
        // Invalid checksums, truncation, unsupported MP entry types and tables
        // with >256 distinct enabled APIC IDs fail without changing discovery.
        bool DiscoverFromMadt(const void* table, uint32_t availableBytes);
        bool DiscoverFromMpTable(const void* table, uint32_t availableBytes);
    };
} }
#endif
