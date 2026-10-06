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
    enum PhysicalMemoryError {
        PhysicalMemoryOk,
        PhysicalMemoryBadMagic,
        PhysicalMemoryBadBootInfo,
        PhysicalMemoryNoMap,
        PhysicalMemoryBadMap,
        PhysicalMemoryBadReservation,
        PhysicalMemoryNoUsableRam,
        PhysicalMemoryAlreadyInUse
    };
    class PhysicalMemoryManager {
    public:
        static const uint32_t PageSize = 4096;
        static const uint32_t MaximumFrames = 1048576;
    private:
        // Separate eligibility and ownership prevent freeing firmware/kernel pages.
        LowBootstrapPool bootstrap;
        uint32_t eligible[MaximumFrames / 32];
        uint32_t allocated[MaximumFrames / 32];
        uint32_t frameCount;
        uint32_t freeCount;
        uint32_t allocatedCount;
        uint32_t failures;
        bool ready;
        bool mapUsed;
        PhysicalMemoryError error;
        void clear();
        bool fail(PhysicalMemoryError reason);
        void markAvailable(uint64_t address, uint64_t length);
        void markReserved(uint64_t address, uint64_t length, bool includeBootstrap = true);
        bool reserveBootData(const MultibootInfo& info, uint32_t infoAddress);
        bool reserveBuffer(uint32_t address, uint64_t length);
        bool reserveString(uint32_t address);
        bool bit(const uint32_t* bitmap, uint32_t frame) const;
        void set(uint32_t* bitmap, uint32_t frame, bool value);
        PhysicalMemoryManager(const PhysicalMemoryManager&);
        PhysicalMemoryManager& operator=(const PhysicalMemoryManager&);
    public:
        PhysicalMemoryManager();
        // Boot-time identity mapped addresses, exclusive kernelEnd. No heap needed.
        // Extra reservations describe early DMA buffers/MMIO not covered by the map.
        bool initialize(const void* multibootInfo, uint32_t magic,
                        uint32_t kernelStart, uint32_t kernelEnd,
                        const PhysicalRange* extraReservations = 0, uint32_t extraCount = 0);
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
        PhysicalMemoryError getLastError() const;
    };
} }
#endif
