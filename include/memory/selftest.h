#ifndef __GTOS__MEMORY__SELFTEST_H
#define __GTOS__MEMORY__SELFTEST_H
namespace gtos { namespace memory {
    class PhysicalMemoryManager;
    // Preserves frame ownership and counts; intended immediately after boot setup.
    bool RunPhysicalMemorySelfTest(PhysicalMemoryManager& frames);
    // Destructive only to its private scratch arena; preserves the active heap.
    bool RunHeapSelfTest();
} }
#endif
