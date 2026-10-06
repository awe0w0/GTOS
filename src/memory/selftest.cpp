#include <memory/selftest.h>
#include <memorymanagement.h>
#include <memory/physical.h>
#include <memory/criticalsection.h>
using namespace gtos;
namespace gtos { namespace memory {
    bool RunPhysicalMemorySelfTest(PhysicalMemoryManager& frames) {
        InterruptGuard guard;
        const PhysicalMemoryStatistics before = frames.getStatistics();
        if (!before.initialized || before.freeFrames < 3) return false;
        uint32_t single = 0, pair = 0;
        if (!frames.allocate(single)) return false;
        if (!frames.allocateContiguous(2, pair, 2)) { frames.free(single); return false; }
        bool result = !(single & 4095) && !(pair & 8191) && single != pair
            && single != pair + 4096 && frames.isAllocated(single)
            && frames.isAllocated(pair) && frames.isAllocated(pair + 4096)
            && !frames.free(0) && !frames.free(single + 1)
            && !frames.reserveRegion(single, 1);
        if (!frames.free(single)) result = false;
        if (frames.free(single)) result = false;
        if (!frames.freeContiguous(pair, 2)) result = false;
        const PhysicalMemoryStatistics after = frames.getStatistics();
        return result && after.freeFrames == before.freeFrames
            && after.allocatedFrames == before.allocatedFrames;
    }
    bool RunHeapSelfTest() {
        InterruptGuard guard;
        uint8_t scratch[8192] __attribute__((aligned(16)));
        MemoryManager* active = MemoryManager::activeMemoryManager;
        MemoryManager heap((size_t)scratch, sizeof(scratch), false);
        const size_t capacity = heap.getStatistics().freeBytes;
        if (!heap.validate() || heap.malloc(0) || heap.malloc(0xFFFFFFFFu)) return false;
        void* exact = heap.malloc(capacity);
        if (!exact || heap.malloc(1) || !heap.tryFree(exact) || heap.tryFree(exact)) return false;
        void* a = heap.malloc(17);
        void* b = heap.malloc(64);
        void* c = heap.malloc(33);
        if (!a || !b || !c || ((size_t)a & 15) || ((size_t)b & 15) || ((size_t)c & 15)) return false;
        if (heap.tryFree((uint8_t*)b + 16) || heap.tryFree((void*)1) || !heap.tryFree(0)) return false;
        heap.free(a);
        heap.free(c);
        heap.free(b); // Coalesces both neighbors.
        if (!heap.validate() || heap.getStatistics().freeBytes != capacity) return false;
        uint8_t* slots[24];
        size_t sizes[24];
        for (uint32_t i = 0; i < 24; ++i) { slots[i] = 0; sizes[i] = 0; }
        uint32_t random = 0x12345678;
        for (uint32_t step = 0; step < 1024; ++step) {
            for (uint32_t slot = 0; slot < 24; ++slot)
                if (slots[slot]) for (size_t byte = 0; byte < sizes[slot]; ++byte)
                    if (slots[slot][byte] != (uint8_t)(slot + 1)) return false;
            random = random * 1664525u + 1013904223u;
            const uint32_t slot = (random >> 16) % 24;
            if (slots[slot]) {
                if (!heap.tryFree(slots[slot])) return false;
                slots[slot] = 0;
            } else {
                sizes[slot] = 1 + ((random >> 8) & 255);
                slots[slot] = (uint8_t*)heap.malloc(sizes[slot]);
                if (!slots[slot] || ((size_t)slots[slot] & 15)) return false;
                for (size_t byte = 0; byte < sizes[slot]; ++byte) slots[slot][byte] = (uint8_t)(slot + 1);
            }
            if (!heap.validate()) return false;
        }
        for (uint32_t slot = 0; slot < 24; ++slot) heap.free(slots[slot]);
        const HeapStatistics stats = heap.getStatistics();
        return stats.valid && stats.freeBytes == capacity && stats.freeBlocks == 1
            && stats.allocatedBlocks == 0 && MemoryManager::activeMemoryManager == active;
    }
} }
