#ifndef GTOS_STORAGE_READABLE_DISK_H
#define GTOS_STORAGE_READABLE_DISK_H

#include <common/types.h>

namespace gtos { namespace storage {
    // Addresses and lengths are expressed in the device's logical sectors.
    // The destination has room for count * LogicalSectorBytes() bytes.
    class ReadableDisk {
    public:
        virtual uint32_t LogicalSectorBytes() const = 0;
        virtual uint64_t LogicalSectorCount() const = 0;
        virtual bool ReadSectors(uint64_t first, uint32_t count, uint8_t* destination) = 0;

    protected:
        ~ReadableDisk() = default;
    };
} }

#endif
