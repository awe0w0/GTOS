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
        static const uint32_t DynamicBase = 0x80000000u;
        static const uint32_t DynamicLimit = 0xBFFFC000u; // Stack and guards excluded.
        static const uint32_t MaximumRegions = 32;
    private:
        struct Page { uint32_t address, frame, region; bool writable, present; };
        struct Region { uint32_t base, bytes, handle; };
        struct PendingPage { uint32_t address, frame; };
        struct PendingTable { uint32_t index, frame; };
        KernelPaging* kernel;
        PhysicalMemoryManager* frames;
        uint32_t directory, kernelDirectory, pageCount;
        uint32_t tables[1024]; // Owned tables only; borrowed tables stay zero.
        Page pages[MaximumPages]; // Authoritative ownership, never inferred from PTEs.
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
        int FindRegion(uint32_t handle) const;
        bool RegionRange(uint32_t handle, uint32_t offset, uint32_t bytes, uint32_t& base) const;
        uint32_t ConflictEnd(uint32_t base, uint32_t bytes) const;
        bool AllocateFrame(uint32_t& address);
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
        // New zeroed data frames, never caller-supplied physical aliases.
        // Mapping/protection changes require unsealed state and kernel CR3.
        bool MapNewPage(uint32_t virtualAddress, bool writable);
        bool ProtectPage(uint32_t virtualAddress, bool writable);
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
        bool Decommit(uint32_t handle, uint32_t offset, uint32_t bytes);
        // Discard clears committed contents without changing permissions; a
        // hole anywhere in the range rejects the entire operation.
        bool Discard(uint32_t handle, uint32_t offset, uint32_t bytes);
        bool Release(uint32_t handle);
        bool Trim(uint32_t handle, uint32_t newBytes);
        // Read-only query also permits kernel CR3. No output on failure.
        bool QueryRegion(uint32_t handle, ProcessMemoryRegionInfo& info) const;
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
