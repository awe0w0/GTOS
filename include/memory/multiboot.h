#ifndef __GTOS__MEMORY__MULTIBOOT_H
#define __GTOS__MEMORY__MULTIBOOT_H
#include <common/types.h>
namespace gtos { namespace memory {
    const uint32_t MultibootBootMagic = 0x2BADB002;
    struct MultibootMemoryMapEntry {
        uint32_t size; // Bytes after this field; may exceed 20.
        uint64_t address;
        uint64_t length;
        uint32_t type;
    } __attribute__((packed));
    struct MultibootModule {
        uint32_t start;
        uint32_t end;
        uint32_t string;
        uint32_t reserved;
    } __attribute__((packed));
    struct MultibootInfo {
        uint32_t flags;
        uint32_t memLower;
        uint32_t memUpper;
        uint32_t bootDevice;
        uint32_t commandLine;
        uint32_t moduleCount;
        uint32_t modules;
        uint32_t symbols[4];
        uint32_t memoryMapLength;
        uint32_t memoryMap;
        uint32_t drivesLength;
        uint32_t drives;
        uint32_t configurationTable;
        uint32_t bootLoaderName;
        uint32_t apmTable;
        uint32_t vbeControlInfo;
        uint32_t vbeModeInfo;
        uint16_t vbeMode;
        uint16_t vbeInterfaceSegment;
        uint16_t vbeInterfaceOffset;
        uint16_t vbeInterfaceLength;
        uint64_t framebufferAddress;
        uint32_t framebufferPitch;
        uint32_t framebufferWidth;
        uint32_t framebufferHeight;
        uint8_t framebufferBitsPerPixel;
        uint8_t framebufferType;
        uint8_t framebufferColorInfo[6];
    } __attribute__((packed));
    static_assert(sizeof(MultibootInfo) == 116, "Multiboot v1 layout");
    static_assert(__builtin_offsetof(MultibootInfo, memoryMap) == 48, "Multiboot mmap offset");
    static_assert(__builtin_offsetof(MultibootInfo, framebufferAddress) == 88, "Multiboot framebuffer offset");
} }
#endif
