#ifndef __GTOS__MEMORYMANAGEMENT_H
#define __GTOS__MEMORYMANAGEMENT_H

#include <common/types.h>

namespace gtos {
    // Both the header and every returned payload are 16-byte aligned.
    struct MemoryChunk {
        MemoryChunk* next;
        MemoryChunk* prev;
        bool allocated;
        size_t size;
        uint32_t magic;
    } __attribute__((aligned(16)));

    struct HeapStatistics {
        size_t totalBytes;
        size_t usedBytes;
        size_t freeBytes;
        size_t largestFreeBlock;
        uint32_t allocatedBlocks;
        uint32_t freeBlocks;
        uint32_t failedAllocations;
        uint32_t invalidFrees;
        bool valid;
    };

    class MemoryManager {
    protected:
        MemoryChunk* first;
    private:
        size_t arenaStart;
        size_t arenaSize;
        uint32_t failedAllocations;
        uint32_t invalidFrees;
        bool validateUnlocked() const;
        MemoryManager(const MemoryManager&);
        MemoryManager& operator=(const MemoryManager&);
    public:
        static const size_t Alignment = 16;
        static MemoryManager* activeMemoryManager;
        // activate=false is useful for isolated arenas and diagnostics.
        MemoryManager(size_t start, size_t size, bool activate = true);
        ~MemoryManager();
        void* malloc(size_t size);
        void free(void* ptr);
        bool tryFree(void* ptr);
        bool validate() const;
        HeapStatistics getStatistics() const;
    };
}

void* operator new(unsigned size) noexcept;
void* operator new[](unsigned size) noexcept;
void* operator new(unsigned size, void* ptr) noexcept;
void* operator new[](unsigned size, void* ptr) noexcept;
void operator delete(void* ptr) noexcept;
void operator delete[](void* ptr) noexcept;
void operator delete(void* ptr, unsigned size) noexcept;
void operator delete[](void* ptr, unsigned size) noexcept;

#endif
