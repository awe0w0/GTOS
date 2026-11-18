#ifndef __GTOS__MEMORY__PHYSICAL_H
#define __GTOS__MEMORY__PHYSICAL_H
#include <common/types.h>
#include <memory/multiboot.h>
#include <memory/bootstrap.h>
namespace gtos { namespace memory {
    struct PhysicalRange { uint64_t address; uint64_t length; };
    struct PhysicalMemoryStatistics {
        uint32_t addressableFrames; // Includes holes below the highest usable frame.
        uint32_t usableFrames;      // Available RAM after all permanent reservations.
        uint32_t freeFrames;
        uint32_t allocatedFrames;
        uint32_t reservedFrames;    // Includes holes, firmware, boot data and kernel.
        uint32_t failedAllocations;
        uint32_t bootstrapFrames;     // Permanently claimed SIPI pages, also reserved above.
        uint32_t bootstrapFreeFrames; // Boot-only pool; excluded from normal freeFrames.
        bool usedMemoryMap;
        bool initialized;
    };
    struct HighMemoryStatistics {
        uint32_t addressableFrames, usableFrames, freeFrames, allocatedFrames, metadataFrames;
    };
    enum PhysicalMemoryError {
        PhysicalMemoryOk,
        PhysicalMemoryBadMagic,
        PhysicalMemoryBadBootInfo,
        PhysicalMemoryNoMap,
        PhysicalMemoryBadMap,
        PhysicalMemoryBadReservation,
        PhysicalMemoryNoUsableRam,
        PhysicalMemoryAlreadyInUse,
        PhysicalMemoryNoHighMetadata
    };
    class ProcessAddressSpace;
    class PhysicalMemoryManager {
    public:
        static const uint32_t PageSize = 4096;
        static const uint32_t MaximumFrames = 1048576;
    private:
        friend class ProcessAddressSpace;
        // Separate eligibility and ownership prevent freeing firmware/kernel pages.
        LowBootstrapPool bootstrap;
        uint32_t eligible[MaximumFrames / 32];
        uint32_t allocated[MaximumFrames / 32];
        // Words below this bound contain no free eligible frames.
        uint32_t firstFreeWord;
        uint32_t frameCount;
        uint32_t freeCount;
        uint32_t allocatedCount;
        uint32_t failures;
        uint32_t *highEligible, *highAllocated;
        uint32_t highFrameCount, highFirstFreeWord, highFreeCount, highAllocatedCount, highMetadataPages;
        bool ready;
        bool mapUsed;
        uint32_t physicalBits;
        PhysicalMemoryError error;
        void clear();
        bool fail(PhysicalMemoryError reason);
        void markAvailable(uint64_t address, uint64_t length);
        void markReserved(uint64_t address, uint64_t length, bool includeBootstrap = true);
        bool reserveBootData(const MultibootInfo& info, uint32_t infoAddress);
        bool reserveBuffer(uint32_t address, uint64_t length);
        bool reserveString(uint32_t address);
        bool prepareHigh(const MultibootInfo& info, uint32_t addressBits,
                         const PhysicalRange* reservations, uint32_t reservationCount);
        bool highRange(uint64_t address, uint64_t end, bool fullPages,
                       uint32_t& first, uint32_t& last) const;
        bool bit(const uint32_t* bitmap, uint32_t frame) const;
        void set(uint32_t* bitmap, uint32_t frame, bool value);
        // The address-space walker already holds a BSP interrupt guard.
        bool isAllocatedLocked(uint32_t address) const {
            const uint32_t frame = address >> 12;
            return ready && !(address & (PageSize - 1)) && frame < frameCount
                && (allocated[frame >> 5] & (1u << (frame & 31)));
        }
        PhysicalMemoryManager(const PhysicalMemoryManager&);
        PhysicalMemoryManager& operator=(const PhysicalMemoryManager&);
    public:
        PhysicalMemoryManager();
        // Boot-time identity mapped addresses, exclusive kernelEnd. No heap needed.
        // Extra reservations describe early DMA buffers/MMIO not covered by the map.
        // Pass the CPU's physical width to register high RAM for a PAE mapper.
        bool initialize(const void* multibootInfo, uint32_t magic,
                        uint32_t kernelStart, uint32_t kernelEnd,
                        const PhysicalRange* extraReservations = 0, uint32_t extraCount = 0,
                        uint32_t physicalAddressBits = 32, bool efiMemoryMap = false);
        // Permanently claim one E820/firmware-validated SIPI page below 1 MiB.
        // No matching free: another processor may still execute this trampoline.
        bool claimLowBootstrapPage(uint32_t& address);
        bool isBootstrapPage(uint32_t address) const;
        bool allocate(uint32_t& address);
        // maxAddress is inclusive; alignmentPages must be a nonzero power of two.
        bool allocateContiguous(uint32_t pages, uint32_t& address,
                                uint32_t alignmentPages = 1, uint32_t maxAddress = 0xFFFFFFFFu);
        bool free(uint32_t address);
        // Free is page-granular; every frame in the range must be currently allocated.
        bool freeContiguous(uint32_t address, uint32_t pages);
        // Permanent reservation; fails atomically if any frame is allocated.
        bool reserveRegion(uint64_t address, uint64_t length);
        bool isFree(uint32_t address) const;
        bool isAllocated(uint32_t address) const;
        PhysicalMemoryStatistics getStatistics() const;
        // High frames are physical addresses and require a mapping before access.
        bool allocateHigh(uint64_t& address);
        bool freeHigh(uint64_t address);
        bool isHighAllocated(uint64_t address) const;
        bool permitsHighMmio(const MultibootInfo& info, uint64_t address, uint32_t length) const;
        HighMemoryStatistics getHighStatistics() const;
#ifdef GTOS_MEMORY_TEST
        // Host-unit seam. Supplies the memory a firmware map would have described, then leaves every allocator decision
        // to the production code. A test that used this to place a frame itself would be writing the answer it is
        // supposed to be measuring.
        // The free frames are the ones inside the arena the boundary places at a fixed address, so a frame number is also
        // a dereferenceable pointer here. That is the same property an identity map gives the kernel, not a shortcut.
        bool gtos_host_seed(uint32_t addressableFrames, uint32_t firstFreeFrame, uint32_t freeFrames);
#endif
        PhysicalMemoryError getLastError() const;
    };
} }
#endif
