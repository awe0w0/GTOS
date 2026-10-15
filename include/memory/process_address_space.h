#ifndef __GTOS__MEMORY__PROCESS_ADDRESS_SPACE_H
#define __GTOS__MEMORY__PROCESS_ADDRESS_SPACE_H
#include <memory/paging.h>
namespace gtos { namespace memory {
    enum ProcessMemoryError : uint32_t {
        ProcessMemoryOk, ProcessMemoryBadState, ProcessMemoryBadRange,
        ProcessMemoryNoMemory, ProcessMemoryConflict, ProcessMemoryNotMapped,
        ProcessMemoryPermission, ProcessMemoryLimit, ProcessMemorySealed,
        ProcessMemoryActive, ProcessMemoryUnsafeContext, ProcessMemoryBadTemplate,
        ProcessMemoryBadMapping, ProcessMemoryBadAlias,
        ProcessMemoryRegionLimit, ProcessMemoryHandleExhausted
    };
    enum ProcessMemoryProtection : uint32_t {
        ProcessMemoryNone = 0, ProcessMemoryRead = 1, ProcessMemoryReadWrite = 3
    };
    struct ProcessMemoryRegionInfo { uint32_t base, bytes, residentPages; };
    struct ProcessMemoryStatistics {
        uint32_t residentPages, dynamicPages, capacityPages, metadataFrames, tableFrames;
    };
    // BSP-only, non-PAE private user mappings. KernelPaging must be enabled and
    // permanently sealed; it and its allocator must outlive every process.
    // No destructor tears mappings down. Before Destroy(), remove ALL runnable
    // references and deferred interrupt/return frames, then switch to kernel CR3.
    // Interrupt masking is not an SMP lock: APs must never enter this directory.
    class ProcessAddressSpace {
    public:
        static const uint32_t UserBase = 0x40000000u;
        static const uint32_t UserLimit = 0xC0000000u; // Exclusive.
        static const uint32_t MaximumPages = 256;
        // Large ELF pages + root stack + the existing runtime-page budget.
        static const uint32_t MaximumCapacityPages = 4096 + 2 + MaximumPages;
        static const uint32_t DynamicBase = 0x80000000u;
        static const uint32_t DynamicLimit = 0xBFFFC000u; // Stack and guards excluded.
        static const uint32_t MaximumRegions = 32;
    private:
        struct Page { uint32_t address, frame, region; bool writable, present; };
        static_assert(sizeof(Page) == 16, "ownership record wire-independent size");
        struct Region { uint32_t base, bytes, handle; };
        struct PendingPage { uint32_t address, frame; };
        struct PendingTable { uint32_t index, frame; };
        KernelPaging* kernel;
        PhysicalMemoryManager* frames;
        uint32_t directory, kernelDirectory, pageCount;
        uint32_t tables[1024]; // Owned tables only; borrowed tables stay zero.
        Page inlinePages[MaximumPages];
        Page* pages; // Authoritative ownership, never inferred from PTEs.
        uint32_t pageLimit, metadataBase, metadataPages, dynamicLimit;
        Region regions[MaximumRegions];
        // A readable range can contain at most MaximumPages consecutive pages,
        // hence at most two new tables. Transaction scratch never uses the stack.
        PendingPage pendingPages[MaximumPages];
        PendingTable pendingTables[2];
        uint32_t pendingPageCount, pendingTableCount;
        bool prepared, sealed;
        mutable ProcessMemoryError error;
        bool Fail(ProcessMemoryError value) const;
        bool Context(bool allowOwnDirectory) const;
        bool KernelAlias(uint32_t address, uint32_t length, bool writable) const;
        bool Layout(bool complete) const;
        bool Range(uint32_t address, uint32_t length, bool writable) const;
        bool Buffer(uint32_t address, uint32_t length, bool writable) const;
        bool Mutable() const;
        bool RuntimeMutable() const;
        int FindPage(uint32_t address) const;
        int IndexedLayoutPage(uint32_t address) const;
        int FindRegion(uint32_t handle) const;
        bool RegionRange(uint32_t handle, uint32_t offset, uint32_t bytes, uint32_t& base) const;
        uint32_t ConflictEnd(uint32_t base, uint32_t bytes) const;
        bool AllocateFrame(uint32_t& address);
        bool MetadataLayout() const;
        bool MapOwnedPage(uint32_t address, bool writable);
        void RemovePage(uint32_t index);
        void RollbackPending();
        void InvalidatePage(uint32_t address) const;
        void RemoveRegionPages(uint32_t handle, uint32_t base, uint32_t bytes);
        ProcessAddressSpace(const ProcessAddressSpace&);
        ProcessAddressSpace& operator=(const ProcessAddressSpace&);
    public:
        ProcessAddressSpace();
        // Requires kernel CR3 active on BSP. Transactional, including overlap
        // rejection for ANY present kernel PDE in the entire fixed user arena.
        // The allocator must be the identical instance used by sharedKernel.
        bool Prepare(KernelPaging& sharedKernel, PhysicalMemoryManager& allocator);
        // Kernel-only opt-in. Large ownership storage is allocated from checked
        // supervisor identity frames, not from the kernel/trap stack or heap.
        bool Prepare(KernelPaging& sharedKernel, PhysicalMemoryManager& allocator,
                     uint32_t capacityPages);
        // Kernel-selected exclusive dynamic bound, below the root stack guard.
        // Frozen mappings may lie above it; runtime reservations cannot.
        bool Prepare(KernelPaging& sharedKernel, PhysicalMemoryManager& allocator,
                     uint32_t capacityPages, uint32_t dynamicEnd);
        // New zeroed data frames, never caller-supplied physical aliases.
        // Mapping/protection changes require unsealed state and kernel CR3.
        bool MapNewPage(uint32_t virtualAddress, bool writable);
        bool MapNewRange(uint32_t address, uint32_t bytes, bool writable);
        bool ProtectPage(uint32_t virtualAddress, bool writable);
        bool ProtectRange(uint32_t address, uint32_t bytes, bool writable);
        bool UnmapPage(uint32_t virtualAddress);
        // Freeze mappings before exposing the CR3 to a runnable task. Idempotent.
        bool Seal();
        // Runtime operations require sealed state, BSP, and this process's CR3.
        // The test harness also permits kernel CR3, without TLB invalidation.
        // Sizes are positive whole pages; offsets are page aligned. Alignment
        // is a power of two >= 4096. A nonzero hint is exact. Reserve uses no frames.
        // Handles are globally monotonic positive values, never reused on reap.
        // Failure preserves outputs and all existing mappings/content.
        bool Reserve(uint32_t bytes, uint32_t alignment, uint32_t hint,
                     uint32_t& outBase, uint32_t& outHandle);
        // NONE retains committed frames/content, with no present user PTE.
        // R/RW commit holes as zero pages. Other permissions (including EXEC)
        // are refused: non-PAE IA32 cannot provide execute-disable protection.
        bool SetPermissions(uint32_t handle, uint32_t offset, uint32_t bytes, uint32_t protection);
        // Tighten owned resident region-zero pages after whole-range validation.
        // The runtime separately checks loaded data eligibility, excluding code
        // and stacks. Positive whole pages; idempotent, no allocation or RW undo.
        bool SetDataReadOnly(uint32_t address, uint32_t bytes);
        bool Decommit(uint32_t handle, uint32_t offset, uint32_t bytes);
        // Discard clears committed contents without changing permissions; a
        // hole anywhere in the range rejects the entire operation.
        bool Discard(uint32_t handle, uint32_t offset, uint32_t bytes);
        bool Release(uint32_t handle);
        bool Trim(uint32_t handle, uint32_t newBytes);
        // Read-only query also permits kernel CR3. No output on failure.
        bool QueryRegion(uint32_t handle, ProcessMemoryRegionInfo& info) const;
        bool QueryStatistics(ProcessMemoryStatistics& info) const;
        // Full-range validation precedes all copying. Kernel buffers must be
        // mapped trusted supervisor buffers, disjoint from all owned
        // frames and all paging structures. A valid zero-length user address
        // remains inside [UserBase,UserLimit), even when no page is mapped.
        bool ValidateUserRange(uint32_t address, uint32_t length, bool writable) const;
        bool CopyToUser(uint32_t destination, const void* source, uint32_t length);
        bool CopyFromUser(void* destination, uint32_t source, uint32_t length) const;
        uint32_t DirectoryAddress() const;
        bool Prepared() const;
        bool Sealed() const;
        // Explicit deferred reap only. Never frees borrowed kernel frames;
        // refuses active CR3 or inconsistent tables without partial cleanup.
        bool Destroy();
        ProcessMemoryError GetLastError() const;
        static const char* ErrorName(ProcessMemoryError value);
    };
} }
#endif
