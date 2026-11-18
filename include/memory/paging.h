#ifndef __GTOS__MEMORY__PAGING_H
#define __GTOS__MEMORY__PAGING_H
#include <memory/physical.h>
namespace gtos { namespace memory {
    // Every supplied device interval is an explicit, page-aligned permission to
    // map MMIO. RAM, retained boot metadata, and kernel cannot be retyped as MMIO.
    struct PagingDeviceRange {
        uint64_t address;
        uint32_t length;
        // High BARs need a supervisor virtual window. Publish it only on success.
        uint32_t* mappedAddress;
        PagingDeviceRange(uint64_t base = 0, uint32_t bytes = 0, uint32_t* mapped = 0)
            : address(base), length(bytes), mappedAddress(mapped) {}
    };
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
        bool prepared, enabled, sealedForSharing, pae;
    };
    // BSP-only, 4 KiB supervisor mappings. The physical manager and this
    // object must outlive paging. Never free its directory/table frames manually.
    // Interrupt masking is NOT an SMP lock. Share with APs only after sealing;
    // the allocator remains BSP-only and mapped frames must stay alive.
    class KernelPaging {
        friend class ProcessAddressSpace;
        PhysicalMemoryManager* frames;
        uint32_t directory;
        uint32_t directories[4], tables[2048], workerFrames[3];
        uint32_t mappedCount, tableCount, highWindow;
        struct DeviceWindow {
            uint64_t physical;
            uint32_t base, bytes;
            bool matches(uint32_t address, uint64_t entry) const;
        };
        static const uint32_t MaximumDevices = 64;
        DeviceWindow highDevices[MaximumDevices];
        uint32_t highDeviceCount;
        uint32_t savedCr0, savedCr3, savedCr4;
        bool prepared, active, sharedSealed, pae;
        PagingError error;
        bool fail(PagingError value);
        bool ensureTable(uint32_t index);
        bool addPage(uint32_t virtualAddress, uint64_t physicalAddress, uint32_t flags);
        bool reserveHighWindow();
        uint32_t findDeviceWindow(uint32_t bytes) const;
        bool matchesHighDevice(uint32_t address, uint64_t entry) const;
        bool highDevicesIntact() const;
        // Used only by the BSP with interrupts disabled. The worker root keeps
        // the slot absent. The BSP can change its PTE without an AP shootdown.
        void bindHighWindow(uint64_t physicalAddress);
        bool identityBytes(uint32_t address, uint64_t length);
        bool bootMappings(const MultibootInfo* info);
        bool ownsPagingFrame(uint32_t address) const;
        bool mappedBytes(uint32_t address, uint32_t length, bool writable) const;
        uint32_t directoryIndex(uint32_t address) const { return address >> (pae ? 21 : 22); }
        uint32_t leafIndex(uint32_t address) const { return (address >> 12) & (pae ? 511 : 1023); }
        uint64_t entry(uint32_t page, uint32_t index) const {
            const uint32_t* words = (const uint32_t*)(__UINTPTR_TYPE__)page;
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
        // One-way freeze. If requested, give workers an immutable root without
        // the BSP's temporary high-memory window. A new worker root must be
        // prepared before enabling paging. Retains tables even on AP failure.
        bool sealForSharedProcessors(uint32_t* workerDirectory = 0);
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
