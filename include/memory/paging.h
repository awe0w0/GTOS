#ifndef __GTOS__MEMORY__PAGING_H
#define __GTOS__MEMORY__PAGING_H
#include <memory/physical.h>
namespace gtos { namespace memory {
    // Every supplied device interval is an explicit, page-aligned permission to
    // map MMIO. RAM, retained boot metadata, and kernel cannot be retyped as MMIO.
    struct PagingDeviceRange { uint32_t address; uint32_t length; };
    struct PagingConfig {
        uint32_t kernelStart, kernelEnd;       // Page-aligned, exclusive end.
        uint32_t readOnlyStart, readOnlyEnd;   // Text + rodata, whole pages only.
        const MultibootInfo* bootInfo;
        const PagingDeviceRange* devices;
        uint32_t deviceCount;
    };
    enum PagingError {
        PagingOk, PagingBadState, PagingBadRange, PagingNoMemory,
        PagingConflict, PagingNotOwned, PagingPinnedMapping, PagingBadBootInfo,
        PagingUnsafeContext, PagingSealed
    };
    struct PagingMapping {
        uint32_t physicalAddress; // Includes the supplied virtual byte offset.
        bool writable, userAccessible, cacheDisabled, managed;
    };
    struct PagingStatistics {
        uint32_t mappedPages, pageTableFrames, directoryAddress;
        bool prepared, enabled, sealedForSharing;
    };
    // BSP-only, 4 KiB non-PAE supervisor mappings. The physical manager and this
    // object must outlive paging. Never free its directory/table frames manually.
    // Interrupt masking is NOT an SMP lock. Share with APs only after sealing;
    // the allocator remains BSP-only and mapped frames must stay alive.
    class KernelPaging {
        PhysicalMemoryManager* frames;
        uint32_t directory;
        uint32_t tables[1024];
        uint32_t mappedCount, tableCount;
        bool prepared, active, sharedSealed;
        PagingError error;
        bool fail(PagingError value);
        bool addPage(uint32_t virtualAddress, uint32_t physicalAddress, uint32_t flags);
        bool identityBytes(uint32_t address, uint64_t length);
        bool bootMappings(const MultibootInfo* info);
        bool ownsPagingFrame(uint32_t address) const;
        bool mappedBytes(uint32_t address, uint32_t length, bool writable) const;
        void invalidate(uint32_t virtualAddress);
        KernelPaging(const KernelPaging&);
        KernelPaging& operator=(const KernelPaging&);
    public:
        KernelPaging();
        // Transactional: failure frees every directory/table frame it allocated,
        // leaves PG unchanged, and preserves all prior physical allocations.
        bool prepareIdentity(PhysicalMemoryManager& allocator, const PagingConfig& config);
        // Loads CR3 and sets CR0.PG|WP. Requires a mapped stack/GDT/IDT and IF=0.
        bool enable();
        // One-way mapping freeze before CR3 is shared with APs. Retains tables
        // permanently, including partial AP-start failures. This is not a TLB
        // shootdown and does not make the physical allocator safe for AP use.
        bool sealForSharedProcessors();
        // Reclaim all paging frames only before enable(). Active tables live for
        // the kernel lifetime; no silent destructor teardown is performed.
        bool abandon();
        // Maps a borrowed allocated data frame. Caller retains allocation and
        // must unmap every managed alias before freeing it. No user mappings are accepted.
        bool mapOwnedPage(uint32_t virtualAddress, uint32_t physicalAddress, bool writable);
        bool unmapOwnedPage(uint32_t virtualAddress);
        bool protectOwnedPage(uint32_t virtualAddress, bool writable);
        // Byte-address query. False clears the result, including on null/holes.
        bool query(uint32_t virtualAddress, PagingMapping& result) const;
        // Identity check for consumers borrowing this allocator-owned template.
        bool usesAllocator(const PhysicalMemoryManager& allocator) const;
        PagingStatistics getStatistics() const;
        PagingError getLastError() const;
        static const char* ErrorName(PagingError value);
    };
} }
#endif
