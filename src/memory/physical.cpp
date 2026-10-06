#include <memory/physical.h>
#include <memory/criticalsection.h>
using namespace gtos::memory;
namespace {
    const uint64_t Limit = 0x100000000ULL;
    bool checkedRange(uint64_t address, uint64_t length) {
        return length <= ~0ULL - address;
    }
    bool accessibleBuffer(uint32_t address, uint64_t length) {
        return !length || (address && length <= Limit - address);
    }
    uint32_t read32(const uint8_t* bytes) {
        return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8)
             | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    }
}
PhysicalMemoryManager::PhysicalMemoryManager()
    : frameCount(0), freeCount(0), allocatedCount(0), failures(0), ready(false),
      mapUsed(false), error(PhysicalMemoryNoMap) {}
void PhysicalMemoryManager::clear() {
    for (uint32_t i = 0; i < MaximumFrames / 32; ++i) {
        eligible[i] = 0;
        allocated[i] = 0;
    }
    frameCount = freeCount = allocatedCount = failures = 0;
    ready = mapUsed = false;
}
bool PhysicalMemoryManager::fail(PhysicalMemoryError reason) {
    clear();
    error = reason;
    return false;
}
bool PhysicalMemoryManager::bit(const uint32_t* bitmap, uint32_t frame) const {
    return (bitmap[frame >> 5] & (1u << (frame & 31))) != 0;
}
void PhysicalMemoryManager::set(uint32_t* bitmap, uint32_t frame, bool value) {
    if (value) bitmap[frame >> 5] |= 1u << (frame & 31);
    else bitmap[frame >> 5] &= ~(1u << (frame & 31));
}
void PhysicalMemoryManager::markAvailable(uint64_t address, uint64_t length) {
    if (!length || address >= Limit) return;
    uint64_t end = address + length;
    if (end > Limit) end = Limit;
    const uint32_t beginFrame = (uint32_t)((address + PageSize - 1) >> 12);
    const uint32_t endFrame = (uint32_t)(end >> 12);
    for (uint32_t frame = beginFrame; frame < endFrame; ++frame) set(eligible, frame, true);
    if (endFrame > frameCount) frameCount = endFrame;
}
void PhysicalMemoryManager::markReserved(uint64_t address, uint64_t length) {
    if (!length || address >= Limit) return;
    uint64_t end = address + length;
    if (end > Limit) end = Limit;
    const uint32_t beginFrame = (uint32_t)(address >> 12);
    const uint32_t endFrame = (uint32_t)((end + PageSize - 1) >> 12);
    for (uint32_t frame = beginFrame; frame < endFrame; ++frame) set(eligible, frame, false);
}
bool PhysicalMemoryManager::reserveBuffer(uint32_t address, uint64_t length) {
    if (!accessibleBuffer(address, length)) return false;
    markReserved(address, length);
    return true;
}
bool PhysicalMemoryManager::reserveString(uint32_t address) {
    if (!address) return false;
    // Bound malformed metadata scans. A longer boot string fails safely.
    for (uint32_t length = 0; length < 65536 && (uint64_t)address + length < Limit; ++length) {
        if (!((const char*)address)[length]) return reserveBuffer(address, length + 1);
    }
    return false;
}
bool PhysicalMemoryManager::reserveBootData(const MultibootInfo& info, uint32_t infoAddress) {
    if (!reserveBuffer(infoAddress, sizeof(MultibootInfo))) return false;
    if ((info.flags & (1u << 2)) && !reserveString(info.commandLine)) return false;
    if (info.flags & (1u << 3)) {
        if (!reserveBuffer(info.modules, (uint64_t)info.moduleCount * sizeof(MultibootModule)))
            return false;
        const MultibootModule* modules = (const MultibootModule*)info.modules;
        for (uint32_t i = 0; i < info.moduleCount; ++i) {
            if (modules[i].end < modules[i].start) return false;
            if (!reserveBuffer(modules[i].start, (uint64_t)modules[i].end - modules[i].start))
                return false;
            if (modules[i].string && !reserveString(modules[i].string)) return false;
        }
    }
    if ((info.flags & (1u << 4)) && (info.flags & (1u << 5))) return false;
    if ((info.flags & (1u << 4))
        && !reserveBuffer(info.symbols[2], (uint64_t)info.symbols[0] + info.symbols[1]))
        return false;
    if (info.flags & (1u << 5)) {
        const uint32_t count = info.symbols[0], stride = info.symbols[1];
        if (count && stride < 40) return false;
        if (!reserveBuffer(info.symbols[2], (uint64_t)count * stride)) return false;
        // Boot loaders may put non-allocated symbol/string sections outside the image.
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* section = (const uint8_t*)(info.symbols[2] + i * stride);
            const uint32_t address = read32(section + 12), bytes = read32(section + 20);
            if (address && !reserveBuffer(address, bytes)) return false;
        }
    }
    if ((info.flags & (1u << 6)) && !reserveBuffer(info.memoryMap, info.memoryMapLength)) return false;
    if ((info.flags & (1u << 7)) && !reserveBuffer(info.drives, info.drivesLength)) return false;
    // BIOS configuration data has firmware-dependent extent; retain a conservative 64 KiB.
    if ((info.flags & (1u << 8)) && !reserveBuffer(info.configurationTable, 65536)) return false;
    if ((info.flags & (1u << 9)) && !reserveString(info.bootLoaderName)) return false;
    if ((info.flags & (1u << 10)) && !reserveBuffer(info.apmTable, 20)) return false;
    if (info.flags & (1u << 11)) {
        if (!reserveBuffer(info.vbeControlInfo, 512) || !reserveBuffer(info.vbeModeInfo, 256)) return false;
        const uint32_t interfaceAddress = ((uint32_t)info.vbeInterfaceSegment << 4) + info.vbeInterfaceOffset;
        if (!reserveBuffer(interfaceAddress, info.vbeInterfaceLength)) return false;
    }
    if (info.flags & (1u << 12)) {
        const uint64_t bytes = (uint64_t)info.framebufferPitch * info.framebufferHeight;
        if (!checkedRange(info.framebufferAddress, bytes)) return false;
        markReserved(info.framebufferAddress, bytes);
        if (info.framebufferType == 0) {
            const uint32_t palette = read32(info.framebufferColorInfo);
            const uint32_t count = info.framebufferColorInfo[4] | ((uint32_t)info.framebufferColorInfo[5] << 8);
            if (!reserveBuffer(palette, count * 3)) return false;
        }
    }
    return true;
}
bool PhysicalMemoryManager::initialize(const void* multibootInfo, uint32_t magic,
                                       uint32_t kernelStart, uint32_t kernelEnd,
                                       const PhysicalRange* extraReservations, uint32_t extraCount) {
    InterruptGuard guard;
    // Never forget ownership of live pages if called accidentally after boot.
    if (ready && allocatedCount) { error = PhysicalMemoryAlreadyInUse; return false; }
    clear();
    if (magic != MultibootBootMagic) return fail(PhysicalMemoryBadMagic);
    if (!accessibleBuffer((uint32_t)multibootInfo, sizeof(MultibootInfo))
        || !kernelStart || kernelEnd <= kernelStart
        || !accessibleBuffer((uint32_t)extraReservations, (uint64_t)extraCount * sizeof(PhysicalRange)))
        return fail(PhysicalMemoryBadBootInfo);
    const MultibootInfo& info = *(const MultibootInfo*)multibootInfo;
    if (info.flags & (1u << 6)) {
        if (!info.memoryMapLength || !accessibleBuffer(info.memoryMap, info.memoryMapLength))
            return fail(PhysicalMemoryBadMap);
        // Two passes give non-RAM entries priority over overlapping available entries.
        for (uint32_t pass = 0; pass < 2; ++pass) {
            uint32_t offset = 0;
            while (offset < info.memoryMapLength) {
                const uint32_t remaining = info.memoryMapLength - offset;
                if (remaining < sizeof(uint32_t)) return fail(PhysicalMemoryBadMap);
                const MultibootMemoryMapEntry* entry = (const MultibootMemoryMapEntry*)(info.memoryMap + offset);
                if (entry->size < 20 || entry->size > remaining - sizeof(uint32_t))
                    return fail(PhysicalMemoryBadMap);
                if (!checkedRange(entry->address, entry->length)) return fail(PhysicalMemoryBadMap);
                if (!pass && entry->type == 1) markAvailable(entry->address, entry->length);
                if (pass && entry->type != 1) markReserved(entry->address, entry->length);
                offset += entry->size + sizeof(uint32_t);
            }
        }
        mapUsed = true;
    } else if (info.flags & 1) {
        // mem_upper is KiB above 1 MiB, not an absolute top-of-RAM address.
        markAvailable(0x100000, (uint64_t)info.memUpper << 10);
    } else return fail(PhysicalMemoryNoMap);
    markReserved(0, 0x100000); // BIOS, real-mode data, VGA/MMIO and the null frame.
    markReserved(kernelStart, (uint64_t)kernelEnd - kernelStart);
    markReserved((uint32_t)this, sizeof(*this));
    if (!reserveBootData(info, (uint32_t)multibootInfo)) return fail(PhysicalMemoryBadReservation);
    for (uint32_t i = 0; i < extraCount; ++i) {
        if (!checkedRange(extraReservations[i].address, extraReservations[i].length))
            return fail(PhysicalMemoryBadReservation);
        markReserved(extraReservations[i].address, extraReservations[i].length);
    }
    for (uint32_t frame = 0; frame < frameCount; ++frame) if (bit(eligible, frame)) ++freeCount;
    if (!freeCount) return fail(PhysicalMemoryNoUsableRam);
    ready = true;
    error = PhysicalMemoryOk;
    return true;
}
bool PhysicalMemoryManager::allocate(uint32_t& address) { return allocateContiguous(1, address); }
bool PhysicalMemoryManager::allocateContiguous(uint32_t pages, uint32_t& address,
                                               uint32_t alignmentPages, uint32_t maxAddress) {
    InterruptGuard guard;
    address = 0;
    uint32_t limit = (uint32_t)(((uint64_t)maxAddress + 1) >> 12);
    if (limit > frameCount) limit = frameCount;
    if (!ready || !pages || pages > freeCount || pages > limit || !alignmentPages
        || (alignmentPages & (alignmentPages - 1)) || alignmentPages > MaximumFrames) {
        ++failures;
        return false;
    }
    uint32_t candidate = 0;
    while (candidate <= limit - pages) {
        candidate = (candidate + alignmentPages - 1) & ~(alignmentPages - 1);
        if (candidate > limit - pages) break;
        uint32_t count = 0;
        while (count < pages && bit(eligible, candidate + count) && !bit(allocated, candidate + count)) ++count;
        if (count == pages) {
            for (uint32_t i = 0; i < pages; ++i) set(allocated, candidate + i, true);
            freeCount -= pages;
            allocatedCount += pages;
            address = candidate << 12;
            return true;
        }
        candidate += count + 1;
    }
    ++failures;
    return false;
}
bool PhysicalMemoryManager::free(uint32_t address) { return freeContiguous(address, 1); }
bool PhysicalMemoryManager::freeContiguous(uint32_t address, uint32_t pages) {
    InterruptGuard guard;
    const uint32_t start = address >> 12;
    if (!ready || (address & (PageSize - 1)) || !pages || pages > frameCount || start > frameCount - pages)
        return false;
    for (uint32_t i = 0; i < pages; ++i)
        if (!bit(eligible, start + i) || !bit(allocated, start + i)) return false;
    for (uint32_t i = 0; i < pages; ++i) set(allocated, start + i, false);
    freeCount += pages;
    allocatedCount -= pages;
    return true;
}
bool PhysicalMemoryManager::reserveRegion(uint64_t address, uint64_t length) {
    InterruptGuard guard;
    if (!ready || !checkedRange(address, length)) return false;
    if (!length || address >= Limit) return true;
    uint64_t end = address + length;
    if (end > Limit) end = Limit;
    const uint32_t start = (uint32_t)(address >> 12);
    uint32_t limit = (uint32_t)((end + PageSize - 1) >> 12);
    if (limit > frameCount) limit = frameCount;
    for (uint32_t frame = start; frame < limit; ++frame) if (bit(allocated, frame)) return false;
    for (uint32_t frame = start; frame < limit; ++frame) {
        if (bit(eligible, frame)) { set(eligible, frame, false); --freeCount; }
    }
    return true;
}
bool PhysicalMemoryManager::isFree(uint32_t address) const {
    InterruptGuard guard;
    const uint32_t frame = address >> 12;
    return ready && !(address & (PageSize - 1)) && frame < frameCount
        && bit(eligible, frame) && !bit(allocated, frame);
}
bool PhysicalMemoryManager::isAllocated(uint32_t address) const {
    InterruptGuard guard;
    const uint32_t frame = address >> 12;
    return ready && !(address & (PageSize - 1)) && frame < frameCount && bit(allocated, frame);
}
PhysicalMemoryStatistics PhysicalMemoryManager::getStatistics() const {
    InterruptGuard guard;
    PhysicalMemoryStatistics result;
    result.addressableFrames = frameCount;
    result.freeFrames = freeCount;
    result.allocatedFrames = allocatedCount;
    result.usableFrames = freeCount + allocatedCount;
    result.reservedFrames = frameCount - result.usableFrames;
    result.failedAllocations = failures;
    result.usedMemoryMap = mapUsed;
    result.initialized = ready;
    return result;
}
PhysicalMemoryError PhysicalMemoryManager::getLastError() const {
    InterruptGuard guard;
    return error;
}
