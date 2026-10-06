#include <memorymanagement.h>
#include <memory/criticalsection.h>

using namespace gtos;
using gtos::memory::InterruptGuard;
namespace {
    const uint32_t ChunkMagic = 0x47544F53;
    const uint64_t AddressLimit = 0x100000000ULL;
}
MemoryManager* MemoryManager::activeMemoryManager = 0;

MemoryManager::MemoryManager(size_t start, size_t size, bool activate)
    : first(0), arenaStart(0), arenaSize(0), failedAllocations(0), invalidFrees(0) {
    InterruptGuard guard;
    const uint64_t end = (uint64_t)start + size;
    const uint64_t aligned = ((uint64_t)start + Alignment - 1) & ~(uint64_t)(Alignment - 1);
    if (start && end <= AddressLimit && aligned < end) {
        const uint64_t bytes = (end - aligned) & ~(uint64_t)(Alignment - 1);
        if (bytes >= sizeof(MemoryChunk) + Alignment) {
            arenaStart = (size_t)aligned;
            arenaSize = (size_t)bytes;
            first = (MemoryChunk*)arenaStart;
            first->allocated = false;
            first->next = 0;
            first->prev = 0;
            first->size = arenaSize - sizeof(MemoryChunk);
            first->magic = ChunkMagic;
        }
    }
    if (activate) activeMemoryManager = this;
}
MemoryManager::~MemoryManager() {
    InterruptGuard guard;
    if (activeMemoryManager == this) activeMemoryManager = 0;
}
bool MemoryManager::validateUnlocked() const {
    if (!first || !arenaSize || (size_t)first != arenaStart) return false;
    uint64_t expected = arenaStart;
    const uint64_t end = (uint64_t)arenaStart + arenaSize;
    const MemoryChunk* previous = 0;
    const MemoryChunk* chunk = first;
    // Contiguous strictly advancing blocks bound traversal even if links corrupt.
    while (chunk) {
        if ((uint64_t)(size_t)chunk != expected || expected + sizeof(MemoryChunk) > end)
            return false;
        if (((const uint8_t*)chunk)[__builtin_offsetof(MemoryChunk, allocated)] > 1
            || chunk->magic != ChunkMagic || chunk->prev != previous
            || chunk->size < Alignment || (chunk->size & (Alignment - 1)))
            return false;
        if (previous && !previous->allocated && !chunk->allocated) return false;
        expected += sizeof(MemoryChunk) + (uint64_t)chunk->size;
        if (expected > end) return false;
        if (expected == end) return chunk->next == 0;
        if (!chunk->next || (uint64_t)(size_t)chunk->next != expected) return false;
        previous = chunk;
        chunk = chunk->next;
    }
    return false;
}
bool MemoryManager::validate() const {
    InterruptGuard guard;
    return validateUnlocked();
}
void* MemoryManager::malloc(size_t size) {
    InterruptGuard guard;
    if (!size || size > 0xFFFFFFFFu - (Alignment - 1) || !validateUnlocked()) {
        ++failedAllocations;
        return 0;
    }
    size = (size + Alignment - 1) & ~(Alignment - 1);
    for (MemoryChunk* chunk = first; chunk; chunk = chunk->next) {
        if (chunk->allocated || chunk->size < size) continue;
        const size_t remainder = chunk->size - size;
        if (remainder >= sizeof(MemoryChunk) + Alignment) {
            MemoryChunk* next = (MemoryChunk*)((size_t)chunk + sizeof(MemoryChunk) + size);
            next->allocated = false;
            next->size = remainder - sizeof(MemoryChunk);
            next->prev = chunk;
            next->next = chunk->next;
            next->magic = ChunkMagic;
            if (next->next) next->next->prev = next;
            chunk->next = next;
            chunk->size = size;
        }
        chunk->allocated = true;
        return (void*)((size_t)chunk + sizeof(MemoryChunk));
    }
    ++failedAllocations;
    return 0;
}
bool MemoryManager::tryFree(void* ptr) {
    InterruptGuard guard;
    if (!ptr) return true;
    const size_t address = (size_t)ptr;
    if ((address & (Alignment - 1)) || address < arenaStart
        || (uint64_t)address >= (uint64_t)arenaStart + arenaSize || !validateUnlocked()) {
        ++invalidFrees;
        return false;
    }
    // Search real block starts; never trust metadata before an arbitrary pointer.
    MemoryChunk* chunk = first;
    while (chunk && (size_t)chunk + sizeof(MemoryChunk) != address) chunk = chunk->next;
    if (!chunk || !chunk->allocated) {
        ++invalidFrees;
        return false;
    }
    chunk->allocated = false;
    if (chunk->prev && !chunk->prev->allocated) {
        MemoryChunk* previous = chunk->prev;
        previous->size += sizeof(MemoryChunk) + chunk->size;
        previous->next = chunk->next;
        if (chunk->next) chunk->next->prev = previous;
        chunk->magic = 0;
        chunk = previous;
    }
    if (chunk->next && !chunk->next->allocated) {
        MemoryChunk* next = chunk->next;
        chunk->size += sizeof(MemoryChunk) + next->size;
        chunk->next = next->next;
        if (chunk->next) chunk->next->prev = chunk;
        next->magic = 0;
    }
    return true;
}
void MemoryManager::free(void* ptr) { (void)tryFree(ptr); }
HeapStatistics MemoryManager::getStatistics() const {
    InterruptGuard guard;
    HeapStatistics result = {};
    result.totalBytes = arenaSize;
    result.failedAllocations = failedAllocations;
    result.invalidFrees = invalidFrees;
    result.valid = validateUnlocked();
    if (!result.valid) return result;
    for (MemoryChunk* chunk = first; chunk; chunk = chunk->next) {
        if (chunk->allocated) {
            result.usedBytes += chunk->size;
            ++result.allocatedBlocks;
        } else {
            result.freeBytes += chunk->size;
            ++result.freeBlocks;
            if (chunk->size > result.largestFreeBlock) result.largestFreeBlock = chunk->size;
        }
    }
    return result;
}
void* operator new(unsigned size) noexcept {
    return MemoryManager::activeMemoryManager ? MemoryManager::activeMemoryManager->malloc(size) : 0;
}
void* operator new[](unsigned size) noexcept { return ::operator new(size); }
void* operator new(unsigned, void* ptr) noexcept { return ptr; }
void* operator new[](unsigned, void* ptr) noexcept { return ptr; }
void operator delete(void* ptr) noexcept {
    if (MemoryManager::activeMemoryManager) MemoryManager::activeMemoryManager->free(ptr);
}
void operator delete[](void* ptr) noexcept { ::operator delete(ptr); }
void operator delete(void* ptr, unsigned) noexcept { ::operator delete(ptr); }
void operator delete[](void* ptr, unsigned) noexcept { ::operator delete(ptr); }
