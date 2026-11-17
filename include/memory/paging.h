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
        uint64_t physicalAddress; // Includes the supplied virtual byte offset.
        bool writable, userAccessible, cacheDisabled, managed;
    };
    struct PagingStatistics {
        uint32_t mappedPages, pageTableFrames, directoryAddress;
        bool prepared, enabled, sealedForSharing;
    };
    // BSP-only, 4 KiB supervisor mappings. The physical manager and this
    // object must outlive paging. Never free its directory/table frames manually.
    // Interrupt masking is NOT an SMP lock. Share with APs only after sealing;
    // the allocator remains BSP-only and mapped frames must stay alive.
    class KernelPaging {
        friend class ProcessAddressSpace;
        PhysicalMemoryManager* frames;
        uint32_t directory;
        uint32_t directories[4], tables[2048];
        uint32_t mappedCount, tableCount, highWindow;
        uint32_t savedCr0, savedCr3, savedCr4;
        bool prepared, active, sharedSealed, pae;
        PagingError error;
        bool fail(PagingError value);
        bool ensureTable(uint32_t index);
        bool addPage(uint32_t virtualAddress, uint64_t physicalAddress, uint32_t flags);
        bool reserveHighWindow();
        // Used only by the BSP with interrupts disabled. APs never access this
        // address. Its directory entry stays fixed even while its PTE changes.
        void bindHighWindow(uint64_t physicalAddress);
        bool identityBytes(uint32_t address, uint64_t length);
        bool bootMappings(const MultibootInfo* info);
        bool ownsPagingFrame(uint32_t address) const;
        bool mappedBytes(uint32_t address, uint32_t length, bool writable) const;
        uint32_t directoryIndex(uint32_t address) const { return address >> (pae ? 21 : 22); }
        uint32_t leafIndex(uint32_t address) const { return (address >> 12) & (pae ? 511 : 1023); }
        uint64_t entry(uint32_t page, uint32_t index) const {
            const uint32_t* words = (const uint32_t*)page;
            return pae ? words[2 * index] | ((uint64_t)words[2 * index + 1] << 32) : words[index];
        }
        void writeEntry(uint32_t page, uint32_t index, uint64_t value);
        uint64_t leafLocked(uint32_t address) const;
        bool queryLocked(uint32_t address, PagingMapping& result) const;
        void invalidate(uint32_t virtualAddress);
        KernelPaging(const KernelPaging&);
        KernelPaging& operator=(const KernelPaging&);
    public:
        KernelPaging();
        // Transactional: failure frees every directory/table frame it allocated,
        // leaves PG unchanged, and preserves all prior physical allocations.
        bool prepareIdentity(PhysicalMemoryManager& allocator, const PagingConfig& config,
                             bool usePae = false);
        // Loads CR3 and sets CR0.PG|WP. Requires a mapped stack/GDT/IDT and IF=0.
        bool enable();
        // End a private boot-time PAE session before any other CPU or address
        // space shares it. Requires IF=0 and the exact active CR3.
        bool retireBootstrapPae();
        // One-way mapping freeze before CR3 is shared with APs. Retains tables
        // permanently, including partial AP-start failures. This is not a TLB
        // shootdown and does not make the physical allocator safe for AP use.
        bool sealForSharedProcessors();
        // Reclaim all paging frames only before enable(). Active tables live for
        // the kernel lifetime; no silent destructor teardown is performed.
        bool abandon();
        // Maps a borrowed allocated data frame. Caller retains allocation and
        // must unmap every managed alias before freeing it. No user mappings are accepted.
        bool mapOwnedPage(uint32_t virtualAddress, uint64_t physicalAddress, bool writable);
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
