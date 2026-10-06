#ifndef __GTOS__MEMORY__BOOTSTRAP_H
#define __GTOS__MEMORY__BOOTSTRAP_H
#include <common/types.h>
namespace gtos { namespace memory {
    // Boot-only SIPI pool. Normal physical allocations never see these pages.
    // Callers serialize access; PhysicalMemoryManager supplies the IRQ guard.
    class LowBootstrapPool {
        uint32_t available[8];
        uint32_t claimed[8];
        uint32_t claimedCount;
        bool firmwareBoundsValid;
        void range(uint64_t address, uint64_t length, uint32_t& first, uint32_t& last) const;
    public:
        LowBootstrapPool();
        void clear();
        void addAvailable(uint64_t address, uint64_t length);
        void reserve(uint64_t address, uint64_t length);
        // Each BIOS source is required and checked. Multiboot KB may be 0 if absent.
        // A zero EBDA pointer means no EBDA; otherwise reserve from EBDA to 0xA0000.
        bool restrictFirmware(uint32_t conventionalKB, uint32_t ebdaAddress,
                              uint32_t multibootLowerKB = 0);
        bool claim(uint32_t& address);
        bool intersectsClaimed(uint64_t address, uint64_t length) const;
        bool isClaimed(uint32_t address) const;
        uint32_t freePages() const;
        uint32_t claimedPages() const;
    };
} }
#endif
