#include <memory/bootstrap.h>
using namespace gtos::memory;
LowBootstrapPool::LowBootstrapPool() { clear(); }
void LowBootstrapPool::clear() {
    for (uint32_t i = 0; i < 8; ++i) available[i] = claimed[i] = 0;
    claimedCount = 0;
    firmwareBoundsValid = false;
}
void LowBootstrapPool::range(uint64_t address, uint64_t length, uint32_t& first, uint32_t& last) const {
    first = last = 0;
    if (!length || address >= 0x100000 || length > ~0ULL - address) return;
    uint64_t end = address + length;
    if (end > 0x100000) end = 0x100000;
    first = (uint32_t)(address >> 12);
    last = (uint32_t)((end + 4095) >> 12);
}
void LowBootstrapPool::addAvailable(uint64_t address, uint64_t length) {
    if (firmwareBoundsValid || claimedCount || !length || address >= 0x100000 || length > ~0ULL - address) return;
    uint64_t end = address + length;
    if (end > 0x100000) end = 0x100000;
    const uint32_t first = (uint32_t)((address + 4095) >> 12);
    const uint32_t last = (uint32_t)(end >> 12);
    for (uint32_t page = first; page < last; ++page)
        if (!(claimed[page >> 5] & (1u << (page & 31))))
            available[page >> 5] |= 1u << (page & 31);
}
void LowBootstrapPool::reserve(uint64_t address, uint64_t length) {
    uint32_t first, last;
    range(address, length, first, last);
    for (uint32_t page = first; page < last; ++page)
        available[page >> 5] &= ~(1u << (page & 31));
}
bool LowBootstrapPool::restrictFirmware(uint32_t conventionalKB, uint32_t ebdaAddress,
                                        uint32_t multibootLowerKB) {
    // Never rewrite ownership after a trampoline was handed to a processor.
    if (claimedCount) return false;
    firmwareBoundsValid = false;
    if (conventionalKB < 64 || conventionalKB > 640
        || (multibootLowerKB && (multibootLowerKB < 64 || multibootLowerKB > 640))
        || (ebdaAddress && (ebdaAddress < 0x10000 || ebdaAddress >= 0xA0000))) {
        for (uint32_t i = 0; i < 8; ++i) available[i] = 0;
        return false;
    }
    uint32_t end = conventionalKB << 10;
    if (multibootLowerKB && (multibootLowerKB << 10) < end) end = multibootLowerKB << 10;
    if (ebdaAddress && ebdaAddress < end) end = ebdaAddress;
    reserve(0, 0x10000); // IVT/BDA and conservative firmware/real-mode scratch margin.
    reserve(end, 0x100000 - end);
    firmwareBoundsValid = true;
    return true;
}
bool LowBootstrapPool::claim(uint32_t& address) {
    address = 0;
    if (!firmwareBoundsValid) return false;
    for (uint32_t page = 16; page < 160; ++page) {
        const uint32_t mask = 1u << (page & 31);
        if (available[page >> 5] & mask) {
            available[page >> 5] &= ~mask;
            claimed[page >> 5] |= mask;
            ++claimedCount;
            address = page << 12;
            return true;
        }
    }
    return false;
}
bool LowBootstrapPool::intersectsClaimed(uint64_t address, uint64_t length) const {
    uint32_t first, last;
    range(address, length, first, last);
    for (uint32_t page = first; page < last; ++page)
        if (claimed[page >> 5] & (1u << (page & 31))) return true;
    return false;
}
bool LowBootstrapPool::isClaimed(uint32_t address) const {
    const uint32_t page = address >> 12;
    return !(address & 4095) && page < 256 && (claimed[page >> 5] & (1u << (page & 31)));
}
uint32_t LowBootstrapPool::freePages() const {
    if (!firmwareBoundsValid) return 0;
    uint32_t result = 0;
    for (uint32_t page = 16; page < 160; ++page)
        if (available[page >> 5] & (1u << (page & 31))) ++result;
    return result;
}
uint32_t LowBootstrapPool::claimedPages() const { return claimedCount; }
