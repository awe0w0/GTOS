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
    uint16_t readBiosWord(uint32_t address) {
#ifdef GTOS_MEMORY_TEST
        // The host harness has no BIOS mapping. Policy tests exercise arbitrary
        // firmware bounds separately; real BIOS reads are verified in QEMU boot.
        return address == 0x413 ? 640 : 0x9FC0;
#else
        uint16_t value;
        asm volatile("movw (%1), %0" : "=r"(value) : "r"(address) : "memory");
        return value;
#endif
    }
    uint32_t read32(const uint8_t* bytes) {
        return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8)
             | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    }
    template<class Visitor>
    bool visitMap(const MultibootInfo& info, Visitor visitor) {
        if (!info.memoryMapLength || !accessibleBuffer(info.memoryMap, info.memoryMapLength))
            return false;
        uint32_t offset = 0;
        while (offset < info.memoryMapLength) {
            const uint32_t remaining = info.memoryMapLength - offset;
            if (remaining < sizeof(uint32_t)) return false;
            const auto* entry = (const MultibootMemoryMapEntry*)(info.memoryMap + offset);
            if (entry->size < 20 || entry->size > remaining - sizeof(uint32_t) ||
                !checkedRange(entry->address, entry->length))
                return false;
            visitor(*entry);
            offset += entry->size + sizeof(uint32_t);
        }
        return true;
    }
}
PhysicalMemoryManager::PhysicalMemoryManager()
    : firstFreeWord(0), frameCount(0), freeCount(0), allocatedCount(0), failures(0),
      highEligible(0), highAllocated(0), highFrameCount(0), highFirstFreeWord(0),
      highFreeCount(0), highAllocatedCount(0), highMetadataPages(0), ready(false),
      mapUsed(false), error(PhysicalMemoryNoMap) {}
void PhysicalMemoryManager::clear() {
    bootstrap.clear();
    for (uint32_t i = 0; i < MaximumFrames / 32; ++i) {
        eligible[i] = 0;
        allocated[i] = 0;
    }
    firstFreeWord = frameCount = freeCount = allocatedCount = failures = 0;
    highEligible = highAllocated = 0;
    highFrameCount = highFirstFreeWord = highFreeCount = highAllocatedCount = highMetadataPages = 0;
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
    bootstrap.addAvailable(address, length);
    if (!length || address >= Limit) return;
    uint64_t end = address + length;
    if (end > Limit) end = Limit;
    const uint32_t beginFrame = (uint32_t)((address + PageSize - 1) >> 12);
    const uint32_t endFrame = (uint32_t)(end >> 12);
    for (uint32_t frame = beginFrame; frame < endFrame; ++frame) set(eligible, frame, true);
    if (endFrame > frameCount) frameCount = endFrame;
}
void PhysicalMemoryManager::markReserved(uint64_t address, uint64_t length, bool includeBootstrap) {
    if (includeBootstrap) bootstrap.reserve(address, length);
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
                                       const PhysicalRange* extraReservations, uint32_t extraCount,
                                       uint32_t physicalAddressBits) {
    InterruptGuard guard;
    // Never forget ownership of live pages if called accidentally after boot.
    if (ready && (allocatedCount || bootstrap.claimedPages())) { error = PhysicalMemoryAlreadyInUse; return false; }
    clear();
    if (magic != MultibootBootMagic) return fail(PhysicalMemoryBadMagic);
    if (!accessibleBuffer((uint32_t)multibootInfo, sizeof(MultibootInfo))
        || !kernelStart || kernelEnd <= kernelStart || physicalAddressBits < 32 || physicalAddressBits > 52
        || !accessibleBuffer((uint32_t)extraReservations, (uint64_t)extraCount * sizeof(PhysicalRange)))
        return fail(PhysicalMemoryBadBootInfo);
    const MultibootInfo& info = *(const MultibootInfo*)multibootInfo;
    if (info.flags & (1u << 6)) {
        // Non-RAM descriptions override overlapping available entries.
        if (!visitMap(info, [this](const MultibootMemoryMapEntry& entry) {
                if (entry.type == 1) markAvailable(entry.address, entry.length);
            }) || !visitMap(info, [this](const MultibootMemoryMapEntry& entry) {
                if (entry.type != 1) markReserved(entry.address, entry.length);
            })) return fail(PhysicalMemoryBadMap);
        mapUsed = true;
    } else if (info.flags & 1) {
        // mem_upper is KiB above 1 MiB, not an absolute top-of-RAM address.
        markAvailable(0x100000, (uint64_t)info.memUpper << 10);
    } else return fail(PhysicalMemoryNoMap);
    // Low RAM is unavailable to the normal allocator; only the validated SIPI
    // pool can ever claim it. All actual boot/firmware reservations affect both.
    markReserved(0, 0x100000, false);
    markReserved(kernelStart, (uint64_t)kernelEnd - kernelStart);
    markReserved((uint32_t)this, sizeof(*this));
    if (!reserveBootData(info, (uint32_t)multibootInfo)) return fail(PhysicalMemoryBadReservation);
    for (uint32_t i = 0; i < extraCount; ++i) {
        if (!checkedRange(extraReservations[i].address, extraReservations[i].length))
            return fail(PhysicalMemoryBadReservation);
        markReserved(extraReservations[i].address, extraReservations[i].length);
    }
    if (mapUsed) {
        if ((info.flags & 1) && !info.memLower) bootstrap.reserve(0, 0x100000);
        bootstrap.restrictFirmware(readBiosWord(0x413), (uint32_t)readBiosWord(0x40E) << 4,
                                   (info.flags & 1) ? info.memLower : 0);
    }
    for (uint32_t frame = 0; frame < frameCount; ++frame) if (bit(eligible, frame)) ++freeCount;
    if (!freeCount) return fail(PhysicalMemoryNoUsableRam);
    ready = true;
    if (mapUsed && physicalAddressBits > 32 &&
        !prepareHigh(info, physicalAddressBits, extraReservations, extraCount))
        return fail(PhysicalMemoryNoHighMetadata);
    error = PhysicalMemoryOk;
    return true;
}
bool PhysicalMemoryManager::highRange(uint64_t address, uint64_t end, bool fullPages,
                                      uint32_t& first, uint32_t& last) const {
    const uint64_t top = Limit + ((uint64_t)highFrameCount << 12);
    if (address >= end || end <= Limit || address >= top)
        return false;
    if (address < Limit) address = Limit;
    if (end > top) end = top;
    first = (uint32_t)((address - Limit + (fullPages ? PageSize - 1 : 0)) >> 12);
    last = (uint32_t)((end - Limit + (fullPages ? 0 : PageSize - 1)) >> 12);
    return first < last;
}
bool PhysicalMemoryManager::prepareHigh(const MultibootInfo& info, uint32_t addressBits,
                                        const PhysicalRange* reservations, uint32_t reservationCount) {
    // With 36-bit physical memory, two dense bitmaps need at most 3.75 MiB.
    const uint64_t upper = 1ULL << (addressBits < 36 ? addressBits : 36);
    uint64_t top = Limit;
    if (!visitMap(info, [&](const MultibootMemoryMapEntry& entry) {
            if (entry.type != 1 || !entry.length || entry.address >= upper) return;
            uint64_t end = entry.address + entry.length;
            if (end > upper) end = upper;
            end &= ~(uint64_t)(PageSize - 1);
            if (end > top) top = end;
        })) return false;
    if (top == Limit) return true;
    highFrameCount = (uint32_t)((top - Limit) >> 12);
    const uint32_t words = (highFrameCount + 31) / 32;
    highMetadataPages = (words * 2 * sizeof(uint32_t) + PageSize - 1) >> 12;
    uint32_t address;
    if (!allocateContiguous(highMetadataPages, address, 1, 0x3FFFFFFFu)) return false;
    highEligible = (uint32_t*)address;
    highAllocated = highEligible + words;
    for (uint32_t i = 0; i < words * 2; ++i) highEligible[i] = 0;
    const auto mark = [this](uint64_t start, uint64_t end, bool available) {
        uint32_t first, last;
        if (highRange(start, end, available, first, last))
            for (uint32_t i = first; i < last; ++i) set(highEligible, i, available);
    };
    // These passes use the same firmware records already validated above.
    visitMap(info, [&](const MultibootMemoryMapEntry& entry) {
        if (entry.type == 1) mark(entry.address, entry.address + entry.length, true);
    });
    visitMap(info, [&](const MultibootMemoryMapEntry& entry) {
        if (entry.type != 1) mark(entry.address, entry.address + entry.length, false);
    });
    if (info.flags & (1u << 12))
        mark(info.framebufferAddress,
             info.framebufferAddress + (uint64_t)info.framebufferPitch * info.framebufferHeight, false);
    for (uint32_t i = 0; i < reservationCount; ++i)
        mark(reservations[i].address, reservations[i].address + reservations[i].length, false);
    for (uint32_t i = 0; i < words; ++i) {
        uint32_t free = highEligible[i];
        for (; free; free &= free - 1) ++highFreeCount;
    }
    return true;
}
bool PhysicalMemoryManager::claimLowBootstrapPage(uint32_t& address) {
    InterruptGuard guard;
    address = 0;
    return ready && bootstrap.claim(address);
}
bool PhysicalMemoryManager::isBootstrapPage(uint32_t address) const {
    InterruptGuard guard;
    return ready && bootstrap.isClaimed(address);
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
    if (pages == 1 && alignmentPages == 1) {
        const uint32_t wholeWords = limit / 32;
        const uint32_t tail = limit % 32;
        const uint32_t words = wholeWords + (tail != 0);
        for (uint32_t word = firstFreeWord; word < words; ++word) {
            uint32_t available = eligible[word] & ~allocated[word];
            if (!available) {
                firstFreeWord = word + 1;
                continue;
            }
            if (word == wholeWords) available &= (1U << tail) - 1;
            // Frames above this request's ceiling may still be free.
            if (!available) break;
            const uint32_t bit = __builtin_ctz(available);
            allocated[word] |= 1U << bit;
            --freeCount;
            ++allocatedCount;
            address = (word * 32 + bit) << 12;
            return true;
        }
        ++failures;
        return false;
    }
    uint32_t candidate = firstFreeWord * 32;
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
    if (start / 32 < firstFreeWord) firstFreeWord = start / 32;
    freeCount += pages;
    allocatedCount -= pages;
    return true;
}
bool PhysicalMemoryManager::reserveRegion(uint64_t address, uint64_t length) {
    InterruptGuard guard;
    if (!ready || !checkedRange(address, length) || bootstrap.intersectsClaimed(address, length)) return false;
    if (!length) return true;
    uint32_t highFirst, highLast;
    const bool high = highRange(address, address + length, false, highFirst, highLast);
    if (high)
        for (uint32_t frame = highFirst; frame < highLast; ++frame)
            if (bit(highAllocated, frame)) return false;
    if (address < Limit) {
        uint64_t end = address + length;
        if (end > Limit) end = Limit;
        const uint32_t start = (uint32_t)(address >> 12);
        uint32_t limit = (uint32_t)((end + PageSize - 1) >> 12);
        if (limit > frameCount) limit = frameCount;
        for (uint32_t frame = start; frame < limit; ++frame)
            if (bit(allocated, frame)) return false;
        for (uint32_t frame = start; frame < limit; ++frame) {
            if (bit(eligible, frame)) { set(eligible, frame, false); --freeCount; }
        }
        bootstrap.reserve(address, length);
    }
    if (high) {
        for (uint32_t frame = highFirst; frame < highLast; ++frame)
            if (bit(highEligible, frame)) {
                set(highEligible, frame, false);
                --highFreeCount;
            }
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
    return isAllocatedLocked(address);
}
bool PhysicalMemoryManager::allocateHigh(uint64_t& address) {
    InterruptGuard guard;
    address = 0;
    if (!ready || !highFreeCount) return false;
    const uint32_t words = (highFrameCount + 31) / 32;
    for (uint32_t word = highFirstFreeWord; word < words; ++word) {
        const uint32_t available = highEligible[word] & ~highAllocated[word];
        if (!available) continue;
        const uint32_t page = __builtin_ctz(available);
        highAllocated[word] |= 1u << page;
        highFirstFreeWord = word;
        --highFreeCount;
        ++highAllocatedCount;
        address = Limit + ((uint64_t)(word * 32 + page) << 12);
        return true;
    }
    return false;
}
bool PhysicalMemoryManager::freeHigh(uint64_t address) {
    InterruptGuard guard;
    if (!isHighAllocated(address)) return false;
    const uint32_t frame = (uint32_t)((address - Limit) >> 12);
    set(highAllocated, frame, false);
    if (frame / 32 < highFirstFreeWord) highFirstFreeWord = frame / 32;
    ++highFreeCount;
    --highAllocatedCount;
    return true;
}
bool PhysicalMemoryManager::isHighAllocated(uint64_t address) const {
    InterruptGuard guard;
    if (!ready || address < Limit || (address & (PageSize - 1))) return false;
    const uint64_t frame = (address - Limit) >> 12;
    return frame < highFrameCount && bit(highAllocated, (uint32_t)frame);
}
HighMemoryStatistics PhysicalMemoryManager::getHighStatistics() const {
    InterruptGuard guard;
    return {highFrameCount, highFreeCount + highAllocatedCount, highFreeCount,
            highAllocatedCount, highMetadataPages};
}
#ifdef GTOS_MEMORY_TEST
bool PhysicalMemoryManager::gtos_host_seed(uint32_t addressable, uint32_t firstFree, uint32_t free) {
    clear();
    if (!addressable || addressable > MaximumFrames) return false;
    if (!free || firstFree + free > addressable) return false;
    frameCount = addressable;
    for (uint32_t frame = firstFree; frame < firstFree + free; ++frame) set(eligible, frame, true);
    freeCount = free;
    allocatedCount = 0;
    failures = 0;
    ready = true;
    mapUsed = false;
    error = PhysicalMemoryOk;
    return true;
}
#endif
PhysicalMemoryStatistics PhysicalMemoryManager::getStatistics() const {
    InterruptGuard guard;
    PhysicalMemoryStatistics result;
    result.addressableFrames = frameCount;
    result.freeFrames = freeCount;
    result.allocatedFrames = allocatedCount;
    result.usableFrames = freeCount + allocatedCount;
    result.reservedFrames = frameCount - result.usableFrames;
    result.failedAllocations = failures;
    result.bootstrapFrames = bootstrap.claimedPages();
    result.bootstrapFreeFrames = bootstrap.freePages();
    result.usedMemoryMap = mapUsed;
    result.initialized = ready;
    return result;
}
PhysicalMemoryError PhysicalMemoryManager::getLastError() const {
    InterruptGuard guard;
    return error;
}
