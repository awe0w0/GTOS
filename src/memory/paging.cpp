#include <memory/paging.h>
#include <memory/criticalsection.h>
using namespace gtos::memory;
namespace {
    const uint32_t Present = 1, Writable = 2, User = 4;
    const uint32_t Uncached = 0x18, Managed = 0x200, AddressMask = 0xFFFFF000u;
    const uint64_t PhysicalMask = 0x000FFFFFFFFFF000ULL;
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
    tableCount(0), highWindow(0), highDeviceCount(0), savedCr0(0), savedCr3(0), savedCr4(0), prepared(false), active(false),
    sharedSealed(false), pae(false), error(PagingOk) {
    for (uint32_t& page : directories) page = 0;
    for (uint32_t& page : tables) page = 0;
    for (uint32_t& page : workerFrames) page = 0;
}
bool KernelPaging::fail(PagingError value) { error = value; return false; }
bool KernelPaging::ownsPagingFrame(uint32_t address) const {
    if (address == directory) return true;
    for (uint32_t page : directories) if (page == address) return true;
    for (uint32_t page : tables) if (page == address) return true;
    for (uint32_t page : workerFrames) if (page == address) return true;
    return false;
}
void KernelPaging::writeEntry(uint32_t page, uint32_t index, uint64_t value) {
    volatile uint32_t* words = (volatile uint32_t*)page;
    if (pae) {
        // These entries are private to the BSP during mutation. Publish the
        // present bit after the physical address has been written.
        words[2 * index] = 0;
        words[2 * index + 1] = (uint32_t)(value >> 32);
        words[2 * index] = (uint32_t)value;
    } else {
        words[index] = (uint32_t)value;
    }
}
uint64_t KernelPaging::leafLocked(uint32_t address) const {
    const uint32_t table = tables[directoryIndex(address)];
    return directory && table ? entry(table, leafIndex(address)) : 0;
}
bool KernelPaging::ensureTable(uint32_t di) {
    if (!tables[di]) {
        uint32_t frame;
        if (!frames->allocate(frame)) return fail(PagingNoMemory);
        clearPage(frame);
        tables[di] = frame;
        if (pae) writeEntry(directories[di / 512], di % 512, frame | Present | Writable);
        else writeEntry(directory, di, frame | Present | Writable);
        ++tableCount;
    }
    return true;
}
bool KernelPaging::addPage(uint32_t virtualAddress, uint64_t physicalAddress, uint32_t flags) {
    const uint32_t di = directoryIndex(virtualAddress), ti = leafIndex(virtualAddress);
    if (!ensureTable(di)) return false;
    if (entry(tables[di], ti) & Present) return fail(PagingConflict);
    writeEntry(tables[di], ti, physicalAddress | Present | flags);
    ++mappedCount;
    invalidate(virtualAddress);
    return true;
}
uint32_t KernelPaging::findDeviceWindow(uint32_t bytes) const {
    uint32_t available = 0;
    // Native ELF, heap and stack addresses are below this supervisor interval.
    for (uint64_t page = 0xC0000000ULL; page < AddressLimit; page += 4096) {
        if (leafLocked((uint32_t)page)) {
            available = 0;
        } else {
            available += 4096;
            if (available == bytes) return (uint32_t)(page + 4096 - bytes);
        }
    }
    return 0;
}
bool KernelPaging::DeviceWindow::matches(uint32_t address, uint64_t entry) const {
    if (address < base || address - base >= bytes) return false;
    const uint64_t expected = (physical + (address - base)) | Present | Writable | Uncached;
    return (entry & ~0x60ULL) == expected; // Hardware may set accessed and dirty.
}
bool KernelPaging::matchesHighDevice(uint32_t address, uint64_t entry) const {
    for (uint32_t i = 0; i < highDeviceCount; ++i)
        if (highDevices[i].matches(address, entry)) return true;
    return false;
}
bool KernelPaging::highDevicesIntact() const {
    for (uint32_t i = 0; i < highDeviceCount; ++i) {
        const DeviceWindow& window = highDevices[i];
        for (uint32_t offset = 0; offset < window.bytes; offset += 4096) {
            const uint32_t address = window.base + offset;
            if (!window.matches(address, leafLocked(address))) return false;
        }
    }
    return true;
}
bool KernelPaging::reserveHighWindow() {
    for (uint32_t address = 0xFFFFF000u; address >= 0xF0000000u; address -= 4096) {
        if (leafLocked(address)) continue;
        if (!ensureTable(directoryIndex(address))) return false;
        highWindow = address;
        return true;
    }
    return fail(PagingConflict);
}
void KernelPaging::bindHighWindow(uint64_t physicalAddress) {
    writeEntry(tables[directoryIndex(highWindow)], leafIndex(highWindow),
               physicalAddress ? physicalAddress | Present | Writable : 0);
    invalidate(highWindow);
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
        for (uint32_t& page : tables) if (page) {
            frames->free(page); page = 0;
        }
        for (uint32_t& page : directories) if (page) {
            frames->free(page); page = 0;
        }
        if (directory) frames->free(directory);
    }
    directory = mappedCount = tableCount = 0;
    highWindow = 0;
    highDeviceCount = 0;
    prepared = pae = false;
    frames = 0;
    error = PagingOk;
    return true;
}
bool KernelPaging::prepareIdentity(PhysicalMemoryManager& allocator, const PagingConfig& config,
                                   bool usePae) {
    InterruptGuard guard;
    if (prepared || active || directory || pagingAlreadyEnabled()
        || !allocator.getStatistics().initialized) return fail(PagingBadState);
    if (!config.kernelStart || !pageAligned(config.kernelStart) || !pageAligned(config.kernelEnd)
        || config.kernelEnd <= config.kernelStart || !pageAligned(config.readOnlyStart)
        || !pageAligned(config.readOnlyEnd) || config.readOnlyStart < config.kernelStart
        || config.readOnlyEnd > config.kernelEnd || config.readOnlyEnd <= config.readOnlyStart
        || config.deviceCount > MaximumDevices || (config.deviceCount && !config.devices))
        return fail(PagingBadRange);
    for (uint32_t i = 0; i < config.deviceCount; ++i) {
        const PagingDeviceRange& device = config.devices[i];
        if (!device.length || (device.address & 4095) || !pageAligned(device.length))
            return fail(PagingBadRange);
        if (device.address < AddressLimit) {
            if (!validBytes((uint32_t)device.address, device.length)) return fail(PagingBadRange);
        } else {
            if (!usePae || !device.mappedAddress || !config.bootInfo ||
                !allocator.permitsHighMmio(*config.bootInfo, device.address, device.length))
                return fail(PagingBadRange);
            for (uint32_t j = 0; j < i; ++j) {
                const PagingDeviceRange& other = config.devices[j];
                if (device.address < other.address + other.length &&
                    other.address < device.address + device.length)
                    return fail(PagingConflict);
            }
        }
    }
    frames = &allocator;
    if (!frames->allocate(directory)) { frames = 0; return fail(PagingNoMemory); }
    pae = usePae;
    clearPage(directory);
    if (pae) {
        // Allocate all four PDPTE targets before loading CR3; these entries
        // are cached by the CPU and do not change during a paging session.
        for (uint32_t i = 0; i < 4; ++i) {
            if (!frames->allocate(directories[i])) {
                abandon();
                return fail(PagingNoMemory);
            }
            clearPage(directories[i]);
            writeEntry(directory, i, directories[i] | Present);
        }
    }
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
    uint32_t deviceBases[MaximumDevices];
    for (uint32_t i = 0; ok && i < config.deviceCount; ++i) {
        const PagingDeviceRange& device = config.devices[i];
        if (device.address >= AddressLimit) continue;
        deviceBases[i] = (uint32_t)device.address;
        const uint64_t end = (uint64_t)device.address + device.length;
        for (uint64_t page = device.address; ok && page < end; page += 4096) {
            // A collision with any RAM/kernel/boot mapping fails closed.
            ok = addPage((uint32_t)page, (uint32_t)page, Writable | Uncached);
        }
    }
    // All identity mappings are present before selecting holes for high BARs.
    for (uint32_t i = 0; ok && i < config.deviceCount; ++i) {
        const PagingDeviceRange& device = config.devices[i];
        if (device.address < AddressLimit) continue;
        deviceBases[i] = findDeviceWindow(device.length);
        if (!deviceBases[i]) { ok = fail(PagingConflict); break; }
        for (uint32_t offset = 0; ok && offset < device.length; offset += 4096)
            ok = addPage(deviceBases[i] + offset, device.address + offset, Writable | Uncached);
    }
    if (ok && pae && frames->getHighStatistics().usableFrames)
        ok = reserveHighWindow();
    if (ok) {
        for (uint32_t page = config.readOnlyStart; page < config.readOnlyEnd; page += 4096) {
            writeEntry(tables[directoryIndex(page)], leafIndex(page), leafLocked(page) & ~uint64_t(Writable));
        }
        for (uint32_t i = 0; i < config.deviceCount; ++i) {
            const PagingDeviceRange& device = config.devices[i];
            if (device.address >= AddressLimit)
                highDevices[highDeviceCount++] = {device.address, deviceBases[i], device.length};
            if (device.mappedAddress) *device.mappedAddress = deviceBases[i];
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
    return queryLocked(address, result);
}
bool KernelPaging::queryLocked(uint32_t address, PagingMapping& result) const {
    result.physicalAddress = 0;
    result.writable = result.userAccessible = result.cacheDisabled = result.managed = false;
    const uint64_t value = leafLocked(address);
    if (!(value & Present)) return false;
    result.physicalAddress = (value & PhysicalMask) | (address & 4095);
    result.writable = (value & Writable) != 0;
    result.userAccessible = (value & User) != 0;
    result.cacheDisabled = (value & 0x10) != 0;
    result.managed = (value & Managed) != 0;
    return true;
}
void KernelPaging::invalidate(uint32_t address) {
#ifndef GTOS_PAGING_TEST
    if (active) asm volatile("invlpg (%0)" : : "r"(address) : "memory");
#else
    (void)address;
#endif
}
bool KernelPaging::mapOwnedPage(uint32_t virtualAddress, uint64_t physicalAddress, bool writable) {
    InterruptGuard guard;
    if (!prepared) return fail(PagingBadState);
    if (sharedSealed) return fail(PagingSealed);
    const bool high = physicalAddress >= AddressLimit;
    if (!virtualAddress || virtualAddress == highWindow || !physicalAddress || !pageAligned(virtualAddress) ||
        (physicalAddress & ~PhysicalMask) || (high && !pae)) return fail(PagingBadRange);
    if (high ? !frames->isHighAllocated(physicalAddress) :
               (!frames->isAllocated((uint32_t)physicalAddress) || ownsPagingFrame((uint32_t)physicalAddress)))
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
    writeEntry(tables[directoryIndex(virtualAddress)], leafIndex(virtualAddress), 0);
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
    const uint64_t old = leafLocked(virtualAddress);
    writeEntry(tables[directoryIndex(virtualAddress)], leafIndex(virtualAddress),
               writable ? (old | Writable) : (old & ~uint64_t(Writable)));
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
    asm volatile("movl %%cr0,%0; movl %%cr3,%1; movl %%cr4,%2"
                 : "=r"(savedCr0), "=r"(savedCr3), "=r"(savedCr4));
    if (!pae && (savedCr4 & (1u << 5))) return fail(PagingUnsafeContext);
    if (pae) {
        uint32_t eax = 1, ebx, ecx, edx;
        asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
        if (!(edx & (1u << 6))) return fail(PagingUnsafeContext);
    }
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
    for (uint32_t page : directories)
        if (page && !mappedBytes(page, 4096, true)) return fail(PagingUnsafeContext);
    for (uint32_t page : tables)
        if (page && !mappedBytes(page, 4096, true)) return fail(PagingUnsafeContext);
#ifndef GTOS_PAGING_TEST
    if (pae) {
        const uint32_t control = savedCr4 | (1u << 5);
        asm volatile("movl %0,%%cr4" : : "r"(control) : "memory");
    }
    asm volatile("movl %0,%%cr3; movl %%cr0,%%eax; orl $0x80010000,%%eax; movl %%eax,%%cr0"
                 : : "r"(directory) : "eax", "memory", "cc");
#endif
    active = true;
    error = PagingOk;
    return true;
}
bool KernelPaging::retireBootstrapPae() {
    if (!active || !pae) return fail(PagingBadState);
    if (sharedSealed) return fail(PagingSealed);
#ifndef GTOS_PAGING_TEST
    uint32_t flags, root;
    asm volatile("pushfl; popl %0; movl %%cr3,%1" : "=r"(flags), "=r"(root) : : "memory");
    if ((flags & (1u << 9)) || root != directory) return fail(PagingUnsafeContext);
    asm volatile("movl %0,%%cr0; movl %1,%%cr4; movl %2,%%cr3"
                 : : "r"(savedCr0), "r"(savedCr4), "r"(savedCr3) : "memory");
#endif
    active = false;
    return abandon();
}
bool KernelPaging::sealForSharedProcessors(uint32_t* workerDirectory) {
    InterruptGuard guard;
    if (workerDirectory) *workerDirectory = 0;
    if (!prepared) return fail(PagingBadState);
    if (workerDirectory && highWindow && !workerFrames[0]) {
        if (active || sharedSealed || pagingAlreadyEnabled() || leafLocked(highWindow))
            return fail(PagingUnsafeContext);
        uint32_t copy[3] = {};
        for (uint32_t& page : copy) {
            if (!frames->allocate(page) || !mappedBytes(page, 4096, true)) {
                const PagingError reason = page ? PagingUnsafeContext : PagingNoMemory;
                for (uint32_t allocated : copy) if (allocated) frames->free(allocated);
                return fail(reason);
            }
        }
        const uint32_t di = directoryIndex(highWindow), pd = di / 512, pde = di % 512;
        clearPage(copy[0]);
        for (uint32_t i = 0; i < 4; ++i)
            writeEntry(copy[0], i, (i == pd ? copy[1] : directories[i]) | Present);
        for (uint32_t i = 0; i < 512; ++i) {
            const uint64_t value = entry(directories[pd], i);
            writeEntry(copy[1], i, i == pde ? copy[2] | (value & ~PhysicalMask) : value);
            writeEntry(copy[2], i, i == leafIndex(highWindow) ? 0 : entry(tables[di], i));
        }
        for (uint32_t i = 0; i < 3; ++i) workerFrames[i] = copy[i];
    }
    sharedSealed = true;
    if (workerDirectory) *workerDirectory = workerFrames[0] ? workerFrames[0] : directory;
    error = PagingOk;
    return true;
}
bool KernelPaging::usesAllocator(const PhysicalMemoryManager& allocator) const {
    InterruptGuard guard;
    return prepared && frames == &allocator;
}
PagingStatistics KernelPaging::getStatistics() const {
    InterruptGuard guard;
    PagingStatistics result = {mappedCount, tableCount, directory, prepared, active, sharedSealed, pae};
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
        case PagingUnsafeContext: return "unsafe context, interrupts enabled, or incompatible paging mode";
        case PagingSealed: return "page tables sealed for shared processors";
        default: return "unknown paging error";
    }
}
