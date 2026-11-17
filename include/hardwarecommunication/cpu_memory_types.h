#ifndef __GTOS__HARDWARECOMMUNICATION__CPU_MEMORY_TYPES_H
#define __GTOS__HARDWARECOMMUNICATION__CPU_MEMORY_TYPES_H

#include <common/types.h>

namespace gtos { namespace hardwarecommunication {
    // BSP-owned snapshot; publish it once and keep it immutable while APs start.
    // Architectural MTRRs/PAT only. The BSP does not change its memory types.
    struct CpuMemoryTypes {
        enum { MaximumVariableMtrrs = 32, FixedMtrrCount = 11 };
        uint32_t valid, cpuidFeatures, physicalAddressBits, variableCount;
        uint64_t capability, defaultType, pat;
        uint64_t variableBase[MaximumVariableMtrrs];
        uint64_t variableMask[MaximumVariableMtrrs];
        // MSR order: 250h, 258h, 259h, 268h through 26Fh.
        uint64_t fixed[FixedMtrrCount];
    };
    struct CpuMemoryTypeFailure {
        enum Reason { None, InvalidEntryState, InvalidSnapshot, DifferentState, CacheTransitionFailed };
        uint32_t reason;
        // MSR number, or a diagnostic identifier for a CPUID field.
        uint32_t index;
        uint64_t expected, observed;
    };

    // Ring 0, protected mode, paging off. The BSP must have CR0.CD/NW clear.
    // Fails closed without CPUID, when advertised MTRR/PAT lacks MSR support,
    // when the variable count exceeds capacity, or on unsupported MTRRCAP bits.
    // MTRR and PAT are optional; absent features have zero register state.
    // Rejects malformed memory-type encodings and requires PAT slot 0 = WB and
    // slot 3 = strong UC, matching the shared paging RAM/MMIO entry attributes.
    // A failure invalidates the destination. Does not alter cache controls.
    bool CaptureMemoryTypes(CpuMemoryTypes& result);

    // Pure predicate for a nonempty, non-wrapping physical byte range that the
    // caller has independently verified is ordinary RAM. Every touched 4 KiB
    // granule must resolve to WB with the shared paging's PAT/PWT/PCD index 0.
    // No MTRRs means legacy WB RAM; supported but disabled MTRRs mean UC.
    // Rejects malformed snapshots, discontinuous/misaligned active MTRRs,
    // undefined overlaps, and all effective types other than WB.
    bool MemoryTypeRangeIsWriteBack(const CpuMemoryTypes& types,
                                    uint32_t physicalAddress, uint32_t length);

    // Ring 0, protected mode, paging off, interrupts already disabled. Validates
    // support and every implemented MTRR/PAT value before enabling caches.
    // Without TCG restoration, mismatch leaves CR0 untouched. Success flushes
    // caches in CD=1/NW=0 no-fill mode and
    // then clears CD/NW, preserving all other CR0 bits and leaving paging off.
    // The optional TCG path accepts only an AP whose MTRRs are entirely reset.
    // It restores those virtual registers before enabling its caches and still
    // requires exact readback. Hardware never takes this path.
    // This is AP startup, not a runtime/global memory-type synchronization API.
    bool PrepareApplicationProcessorMemory(const CpuMemoryTypes& bootstrap,
                                           CpuMemoryTypeFailure* failure = 0,
                                           bool restoreTcgMtrrs = false);
} }

#endif
