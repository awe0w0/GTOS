#ifndef __GTOS__MEMORY__MULTIBOOT2_H
#define __GTOS__MEMORY__MULTIBOOT2_H
#include <memory/multiboot.h>

namespace gtos { namespace memory {
    const uint32_t Multiboot2BootMagic = 0x36D76289;
    // The original tags may be overwritten after this copy. The normalized
    // form owns its map, module descriptors, command line and ACPI pointer.
    class Multiboot2Handoff {
        MultibootInfo info;
        MultibootMemoryMapEntry map[256];
        MultibootModule modules[32];
        char commandLine[256];
        uint8_t rsdp[4096];
        uint32_t rsdpBytes, acpiLimit;
        bool efi, ready;
    public:
        enum { MaximumBytes = 65536 };
        bool read(const void* data, uint32_t available);
        const MultibootInfo* bootInfo() const { return ready ? &info : 0; }
        bool fromEfi() const { return ready && efi; }
        const void* acpi(uint32_t& bytes) const {
            bytes = ready ? rsdpBytes : 0;
            return bytes ? rsdp : 0;
        }
        uint32_t acpiPhysicalLimit() const { return ready ? acpiLimit : 0; }
    };
} }
#endif
