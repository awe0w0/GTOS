#include <memory/paging.h>
#include <memory/criticalsection.h>
using namespace gtos::memory;
namespace {
    const uint32_t Present = 1, Writable = 2, User = 4;
    const uint32_t Uncached = 0x18, Managed = 0x200, AddressMask = 0xFFFFF000u;
    const uint64_t AddressLimit = 0x100000000ULL;
    bool pageAligned(uint32_t address) { return (address & 4095) == 0; }
    bool validBytes(uint32_t address, uint64_t length) {
        return !length || (address >= 4096 && length <= AddressLimit - address);
    }
    bool pagingAlreadyEnabled() {
#ifndef GTOS_PAGING_TEST
        uint32_t cr0; asm volatile("movl %%cr0,%0" : "=r"(cr0));
        return (cr0 & 0x80000000u) != 0;
#else
        return false;
#endif
    }
    void clearPage(uint32_t address) {
        uint32_t* page = (uint32_t*)address;
        for (uint32_t i = 0; i < 1024; ++i) page[i] = 0;
    }
}
KernelPaging::KernelPaging() : frames(0), directory(0), mappedCount(0),
    tableCount(0), prepared(false), active(false), sharedSealed(false), error(PagingOk) {
    for (uint32_t i = 0; i < 1024; ++i) tables[i] = 0;
}
bool KernelPaging::fail(PagingError value) { error = value; return false; }
bool KernelPaging::ownsPagingFrame(uint32_t address) const {
    if (address == directory) return true;
    for (uint32_t i = 0; i < 1024; ++i) if (tables[i] == address) return true;
    return false;
}
bool KernelPaging::addPage(uint32_t virtualAddress, uint32_t physicalAddress, uint32_t flags) {
    const uint32_t di = virtualAddress >> 22, ti = (virtualAddress >> 12) & 1023;
    if (!tables[di]) {
        uint32_t frame;
        if (!frames->allocate(frame)) return fail(PagingNoMemory);
        clearPage(frame);
        tables[di] = frame;
        ((uint32_t*)directory)[di] = frame | Present | Writable;
        ++tableCount;
    }
    uint32_t& entry = ((uint32_t*)tables[di])[ti];
    if (entry & Present) return fail(PagingConflict);
    entry = physicalAddress | Present | flags;
    ++mappedCount;
    invalidate(virtualAddress);
    return true;
}
bool KernelPaging::identityBytes(uint32_t address, uint64_t length) {
    if (!validBytes(address, length)) return fail(PagingBadRange);
    if (!length) return true;
    const uint64_t end = ((uint64_t)address + length + 4095) & ~4095ULL;
    for (uint64_t page = address & AddressMask; page < end; page += 4096) {
        PagingMapping mapping;
        if (query((uint32_t)page, mapping)) {
            if (mapping.physicalAddress != page || mapping.cacheDisabled)
                return fail(PagingConflict);
        } else if (!addPage((uint32_t)page, (uint32_t)page, Writable)) return false;
    }
    return true;
}
bool KernelPaging::bootMappings(const MultibootInfo* info) {
    if (!info || !identityBytes((uint32_t)info, sizeof(*info))) return fail(PagingBadBootInfo);
    // These are the only bootloader-owned structures used after this milestone.
    // Firmware discovery and string/symbol/VBE parsing must finish before PG.
    if ((info->flags & (1u << 6))
        && !identityBytes(info->memoryMap, info->memoryMapLength)) return false;
    if (info->flags & (1u << 3)) {
        if (!identityBytes(info->modules, (uint64_t)info->moduleCount * sizeof(MultibootModule)))
            return false;
        const MultibootModule* modules = (const MultibootModule*)info->modules;
        for (uint32_t i = 0; i < info->moduleCount; ++i) {
            if (modules[i].end < modules[i].start) return fail(PagingBadBootInfo);
            if (!identityBytes(modules[i].start, (uint64_t)modules[i].end - modules[i].start))
                return false;
        }
    }
    return true;
}
bool KernelPaging::abandon() {
    InterruptGuard guard;
    if (sharedSealed) return fail(PagingSealed);
    if (active) return fail(PagingBadState);
    if (frames) {
        for (uint32_t i = 0; i < 1024; ++i) if (tables[i]) {
            frames->free(tables[i]); tables[i] = 0;
        }
        if (directory) frames->free(directory);
    }
    directory = mappedCount = tableCount = 0;
    prepared = false;
    frames = 0;
    error = PagingOk;
    return true;
}
bool KernelPaging::prepareIdentity(PhysicalMemoryManager& allocator, const PagingConfig& config) {
    InterruptGuard guard;
    if (prepared || active || directory || pagingAlreadyEnabled()
        || !allocator.getStatistics().initialized) return fail(PagingBadState);
    if (!config.kernelStart || !pageAligned(config.kernelStart) || !pageAligned(config.kernelEnd)
        || config.kernelEnd <= config.kernelStart || !pageAligned(config.readOnlyStart)
        || !pageAligned(config.readOnlyEnd) || config.readOnlyStart < config.kernelStart
        || config.readOnlyEnd > config.kernelEnd || config.readOnlyEnd <= config.readOnlyStart
        || config.deviceCount > 64 || (config.deviceCount && !config.devices))
        return fail(PagingBadRange);
    for (uint32_t i = 0; i < config.deviceCount; ++i) {
        const PagingDeviceRange& device = config.devices[i];
        if (!device.length || !pageAligned(device.address) || !pageAligned(device.length)
            || !validBytes(device.address, device.length)) return fail(PagingBadRange);
    }
    frames = &allocator;
    if (!frames->allocate(directory)) { frames = 0; return fail(PagingNoMemory); }
    clearPage(directory);
    bool ok = identityBytes(config.kernelStart, (uint64_t)config.kernelEnd - config.kernelStart);
    // The allocator reserves its own bitmap storage even if it lives outside
    // the linked kernel (for example, in an early boot workspace). Retain it.
    if (ok) ok = identityBytes((uint32_t)&allocator, sizeof(allocator));
    const uint32_t count = frames->getStatistics().addressableFrames;
    for (uint32_t i = 1; ok && i < count; ++i) {
        uint32_t page = i << 12;
        if (frames->isFree(page) || frames->isAllocated(page)) ok = identityBytes(page, 4096);
    }
    // Claimed trampoline pages are permanent; unclaimed low RAM stays absent.
    for (uint32_t page = 4096; ok && page < 0x100000; page += 4096)
        if (frames->isBootstrapPage(page)) ok = identityBytes(page, 4096);
    if (ok) ok = bootMappings(config.bootInfo);
    for (uint32_t i = 0; ok && i < config.deviceCount; ++i) {
        const PagingDeviceRange& device = config.devices[i];
        const uint64_t end = (uint64_t)device.address + device.length;
        for (uint64_t page = device.address; ok && page < end; page += 4096) {
            // A collision with any RAM/kernel/boot mapping fails closed.
            ok = addPage((uint32_t)page, (uint32_t)page, Writable | Uncached);
        }
    }
    if (ok) {
        for (uint32_t page = config.readOnlyStart; page < config.readOnlyEnd; page += 4096) {
            uint32_t& entry = ((uint32_t*)tables[page >> 22])[(page >> 12) & 1023];
            entry &= ~Writable;
        }
        prepared = true;
        error = PagingOk;
        return true;
    }
    const PagingError reason = error;
    abandon();
    return fail(reason);
}
bool KernelPaging::query(uint32_t address, PagingMapping& result) const {
    InterruptGuard guard;
    result.physicalAddress = 0;
    result.writable = result.userAccessible = result.cacheDisabled = result.managed = false;
    const uint32_t di = address >> 22;
    if (!directory || !tables[di]) return false;
    const uint32_t entry = ((const uint32_t*)tables[di])[(address >> 12) & 1023];
    if (!(entry & Present)) return false;
    result.physicalAddress = (entry & AddressMask) | (address & 4095);
    result.writable = (entry & Writable) != 0;
    result.userAccessible = (entry & User) != 0;
    result.cacheDisabled = (entry & 0x10) != 0;
    result.managed = (entry & Managed) != 0;
    return true;
}
void KernelPaging::invalidate(uint32_t address) {
#ifndef GTOS_PAGING_TEST
    if (active) asm volatile("invlpg (%0)" : : "r"(address) : "memory");
#else
    (void)address;
#endif
}
bool KernelPaging::mapOwnedPage(uint32_t virtualAddress, uint32_t physicalAddress, bool writable) {
    InterruptGuard guard;
    if (!prepared) return fail(PagingBadState);
    if (sharedSealed) return fail(PagingSealed);
    if (!virtualAddress || !physicalAddress || !pageAligned(virtualAddress)
        || !pageAligned(physicalAddress)) return fail(PagingBadRange);
    if (!frames->isAllocated(physicalAddress) || ownsPagingFrame(physicalAddress))
        return fail(PagingNotOwned);
    if (!addPage(virtualAddress, physicalAddress, Managed | (writable ? Writable : 0))) return false;
    error = PagingOk;
    return true;
}
bool KernelPaging::unmapOwnedPage(uint32_t virtualAddress) {
    InterruptGuard guard;
    if (!prepared) return fail(PagingBadState);
    if (sharedSealed) return fail(PagingSealed);
    if (!virtualAddress || !pageAligned(virtualAddress)) return fail(PagingBadRange);
    PagingMapping mapping;
    if (!query(virtualAddress, mapping) || !mapping.managed) return fail(PagingPinnedMapping);
    ((uint32_t*)tables[virtualAddress >> 22])[(virtualAddress >> 12) & 1023] = 0;
    --mappedCount;
    invalidate(virtualAddress);
    error = PagingOk;
    return true;
}
bool KernelPaging::protectOwnedPage(uint32_t virtualAddress, bool writable) {
    InterruptGuard guard;
    if (!prepared) return fail(PagingBadState);
    if (sharedSealed) return fail(PagingSealed);
    if (!virtualAddress || !pageAligned(virtualAddress)) return fail(PagingBadRange);
    PagingMapping mapping;
    if (!query(virtualAddress, mapping) || !mapping.managed) return fail(PagingPinnedMapping);
    uint32_t& entry = ((uint32_t*)tables[virtualAddress >> 22])[(virtualAddress >> 12) & 1023];
    entry = writable ? (entry | Writable) : (entry & ~Writable);
    invalidate(virtualAddress);
    error = PagingOk;
    return true;
}
bool KernelPaging::mappedBytes(uint32_t address, uint32_t length, bool writable) const {
    if (!validBytes(address, length)) return false;
    const uint64_t end = (uint64_t)address + length;
    for (uint64_t page = address & AddressMask; page < end; page += 4096) {
        PagingMapping mapping;
        if (!query((uint32_t)page, mapping) || mapping.physicalAddress != page
            || mapping.userAccessible || (writable && !mapping.writable)) return false;
    }
    return true;
}
bool KernelPaging::enable() {
    // Do not silently change IF at activation: caller must deliberately arrange
    // the IDT and boot sequence with interrupts disabled.
    if (!prepared || active || pagingAlreadyEnabled()) return fail(PagingBadState);
#ifndef GTOS_PAGING_TEST
    uint32_t cr4; asm volatile("movl %%cr4,%0" : "=r"(cr4));
    // Legacy 1024-entry directories are not PAE PDPTs. PSE/PGE may remain set:
    // all our PDE.PS and PTE.G bits are clear. Any previous PG disable flushed
    // globals; loading CR3 below flushes nonglobal translations.
    if (cr4 & (1u << 5)) return fail(PagingUnsafeContext);
    uint32_t flags, stack, instruction;
    asm volatile("pushfl; popl %0; movl %%esp,%1; call 1f; 1: popl %2"
                 : "=r"(flags), "=r"(stack), "=r"(instruction) : : "memory");
    struct Descriptor { uint16_t limit; uint32_t address; } __attribute__((packed));
    Descriptor gdt, idt;
    asm volatile("sgdt %0; sidt %1" : "=m"(gdt), "=m"(idt));
    if ((flags & (1u << 9)) || !mappedBytes(stack, 4, true)
        || !mappedBytes(instruction, 1, false)
        || !mappedBytes(gdt.address, (uint32_t)gdt.limit + 1, true)
        || !mappedBytes(idt.address, (uint32_t)idt.limit + 1, false))
        return fail(PagingUnsafeContext);
#endif
    if (!mappedBytes((uint32_t)this, sizeof(*this), true)
        || !mappedBytes((uint32_t)frames, sizeof(*frames), true)
        || !mappedBytes(directory, 4096, true)) return fail(PagingUnsafeContext);
    for (uint32_t i = 0; i < 1024; ++i)
        if (tables[i] && !mappedBytes(tables[i], 4096, true)) return fail(PagingUnsafeContext);
#ifndef GTOS_PAGING_TEST
    asm volatile("movl %0,%%cr3; movl %%cr0,%%eax; orl $0x80010000,%%eax; movl %%eax,%%cr0"
                 : : "r"(directory) : "eax", "memory", "cc");
#endif
    active = true;
    error = PagingOk;
    return true;
}
bool KernelPaging::sealForSharedProcessors() {
    InterruptGuard guard;
    if (!prepared) return fail(PagingBadState);
    sharedSealed = true;
    error = PagingOk;
    return true;
}
bool KernelPaging::usesAllocator(const PhysicalMemoryManager& allocator) const {
    InterruptGuard guard;
    return prepared && frames == &allocator;
}
PagingStatistics KernelPaging::getStatistics() const {
    InterruptGuard guard;
    PagingStatistics result = {mappedCount, tableCount, directory, prepared, active, sharedSealed};
    return result;
}
PagingError KernelPaging::getLastError() const { return error; }

const char* KernelPaging::ErrorName(PagingError value) {
    switch (value) {
        case PagingOk: return "ok";
        case PagingBadState: return "invalid paging state";
        case PagingBadRange: return "invalid or unaligned range";
        case PagingNoMemory: return "no page-table memory";
        case PagingConflict: return "mapping collision";
        case PagingNotOwned: return "not an owned data frame";
        case PagingPinnedMapping: return "pinned or absent mapping";
        case PagingBadBootInfo: return "invalid boot metadata";
        case PagingUnsafeContext: return "unsafe context, interrupts enabled, or PAE mode";
        case PagingSealed: return "page tables sealed for shared processors";
        default: return "unknown paging error";
    }
}
