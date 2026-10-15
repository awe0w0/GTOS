#include "heap.h"
#include <process/abi.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <new>

#if !defined(__i386__) || !defined(__GTOS__) || defined(__linux__) || defined(__unix__) || defined(_WIN32)
#error The native heap requires the real GTOS i386 data-page ABI
#endif
static_assert(sizeof(void*) == 4 && sizeof(size_t) == 4, "GTOS IA32 heap ABI");
static_assert(alignof(max_align_t) <= GTOS_NATIVE_HEAP_ALIGNMENT, "C allocation alignment");
static_assert(__STDCPP_DEFAULT_NEW_ALIGNMENT__ <= GTOS_NATIVE_HEAP_ALIGNMENT, "C++ allocation alignment");
namespace {
    const unsigned Header = GTOS_NATIVE_HEAP_HEADER_BYTES;
    const unsigned Alignment = GTOS_NATIVE_HEAP_ALIGNMENT;
    const unsigned Minimum = Alignment;
    const unsigned MaximumPayload = GTOS_NATIVE_HEAP_BYTES - Header;
    const unsigned MaximumBlocks = GTOS_NATIVE_HEAP_BYTES / (Header + Minimum);
    const unsigned Magic = 0x48454150U;
    const unsigned UserBase = 0x40000000U, UserLimit = 0xC0000000U;
    const unsigned ArenaBase = 0x80000000U, ArenaLimit = 0xBFFFC000U;
    struct alignas(16) Block {
        unsigned next, prev, capacity, requested;
        unsigned userOffset, alignment, allocated, magic;
    };
    struct Arena { unsigned base, handle, length, first, liveBlocks; };
    static_assert(sizeof(Block) == Header && alignof(Block) == Alignment, "Native heap block layout");
    Arena arena;
    Arena extraArenas[GTOS_NATIVE_HEAP_ARENAS - 1U];
    struct LargeAllocation { unsigned base, handle, length, requested, alignment; };
    LargeAllocation largeAllocations[GTOS_NATIVE_HEAP_LARGE_SLOTS];
    unsigned largeAllocationsEnabled;

    int Call(unsigned operation, unsigned first, unsigned second) noexcept {
        int result;
        asm volatile("int $0x80" : "=a"(result)
            : "a"(operation), "b"(first), "c"(second) : "memory", "cc");
        return result;
    }
    unsigned heapLock;
    class HeapGuard {
        HeapGuard(const HeapGuard&);
        HeapGuard& operator=(const HeapGuard&);
    public:
        HeapGuard() noexcept {
            unsigned expected = 0;
            while (!__atomic_compare_exchange_n(&heapLock, &expected, 1U, false,
                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                // Only BSP runs user contexts. Let the preempted owner run;
                // no errno/TLS/allocation or callback is used on this path.
                if (Call(GTOS_SYS_YIELD, 0, 0) != 0)
                    gtos_native_heap_panic(GTOS_NATIVE_HEAP_EXIT_VM_CONTRACT);
                expected = 0;
            }
        }
        ~HeapGuard() noexcept { __atomic_store_n(&heapLock, 0U, __ATOMIC_RELEASE); }
    };
    Block* At(unsigned address) noexcept { return (Block*)address; }
    unsigned Address(const Block* block) noexcept { return (unsigned)block; }
    unsigned Payload(const Block* block) noexcept { return Address(block) + Header + block->userOffset; }
    bool PowerOfTwo(unsigned value) noexcept { return value && !(value & (value - 1U)); }
    [[noreturn]] void Corrupt() noexcept { gtos_native_heap_panic(GTOS_NATIVE_HEAP_EXIT_CORRUPT); }
    Arena& ArenaAt(unsigned index) noexcept { return index ? extraArenas[index - 1U] : arena; }
    void Validate(const Arena& owner) noexcept {
        const Arena& arena = owner;
        if (!arena.handle) {
            if (arena.base || arena.length || arena.first || arena.liveBlocks) Corrupt();
            return;
        }
        if (arena.handle > 0x7FFFFFFFU || arena.base < ArenaBase
            || arena.base > ArenaLimit - GTOS_NATIVE_HEAP_BYTES
            || (arena.base & (GTOS_VM_PAGE_BYTES - 1U))
            || arena.length != GTOS_NATIVE_HEAP_BYTES || arena.first != arena.base
            || arena.liveBlocks > MaximumBlocks) Corrupt();
        unsigned address = arena.first, previous = 0, count = 0, live = 0;
        bool previousFree = false;
        const unsigned end = arena.base + arena.length;
        while (address) {
            if (++count > MaximumBlocks || address < arena.base || address > end - Header - Minimum
                || (address & (Alignment - 1U))) Corrupt();
            const Block* block = At(address);
            const unsigned remaining = end - address - Header;
            if (block->magic != Magic || block->prev != previous
                || block->capacity < Minimum || block->capacity > remaining
                || (block->capacity & (Alignment - 1U)) || block->allocated > 1U) Corrupt();
            const unsigned next = address + Header + block->capacity;
            if (block->next != (next == end ? 0U : next)) Corrupt();
            if (block->allocated) {
                if (!block->requested || block->userOffset > block->capacity
                    || block->requested > block->capacity - block->userOffset
                    || !PowerOfTwo(block->alignment) || block->alignment < Alignment
                    || block->userOffset >= block->alignment
                    || (block->userOffset & (Alignment - 1U))
                    || (Payload(block) & (block->alignment - 1U))) Corrupt();
                ++live;
            } else if (previousFree || block->requested || block->userOffset
                || block->alignment != Alignment) Corrupt();
            previousFree = !block->allocated;
            previous = address;
            address = block->next;
        }
        if (!count || live != arena.liveBlocks) Corrupt();
    }
    void ValidateLarge() noexcept {
        if (largeAllocationsEnabled > 1U) Corrupt();
        for (unsigned i = 0; i < GTOS_NATIVE_HEAP_LARGE_SLOTS; ++i) {
            const LargeAllocation& block = largeAllocations[i];
            if (!block.handle) {
                if (block.base || block.length || block.requested || block.alignment) Corrupt();
                continue;
            }
            if (!largeAllocationsEnabled || block.handle > 0x7FFFFFFFU
                || !block.length || block.length > GTOS_NATIVE_HEAP_LARGE_BYTES
                || (block.length & (GTOS_VM_PAGE_BYTES - 1U))
                || block.base < ArenaBase || block.base > ArenaLimit - block.length
                || (block.base & (GTOS_VM_PAGE_BYTES - 1U))
                || !block.requested || block.requested > block.length
                || block.alignment < Alignment || !PowerOfTwo(block.alignment)
                || (block.base & (block.alignment - 1U))) Corrupt();
            for (unsigned j = 0; j < GTOS_NATIVE_HEAP_ARENAS; ++j) {
                const Arena& owner = ArenaAt(j);
                if (owner.handle && (block.handle == owner.handle
                    || (block.base < owner.base + owner.length
                        && owner.base < block.base + block.length))) Corrupt();
            }
            for (unsigned j = 0; j < i; ++j) {
                const LargeAllocation& other = largeAllocations[j];
                if (other.handle && (other.handle == block.handle
                    || (block.base < other.base + other.length
                        && other.base < block.base + block.length))) Corrupt();
            }
        }
    }
    void ValidateAll() noexcept {
        for (unsigned i = 0; i < GTOS_NATIVE_HEAP_ARENAS; ++i) {
            const Arena& owner = ArenaAt(i);
            Validate(owner);
            if (i && owner.handle && !largeAllocationsEnabled) Corrupt();
            if (!owner.handle) continue;
            for (unsigned j = 0; j < i; ++j) {
                const Arena& other = ArenaAt(j);
                if (other.handle && (owner.handle == other.handle
                    || (owner.base < other.base + other.length
                        && other.base < owner.base + owner.length))) Corrupt();
            }
        }
        ValidateLarge();
    }
    Arena& FindArena(const void* pointer) noexcept {
        const unsigned address = (unsigned)pointer;
        for (unsigned i = 0; i < GTOS_NATIVE_HEAP_ARENAS; ++i) {
            Arena& owner = ArenaAt(i);
            if (owner.handle && address >= owner.base && address < owner.base + owner.length)
                return owner;
        }
        gtos_native_heap_panic(GTOS_NATIVE_HEAP_EXIT_BAD_POINTER);
    }
    LargeAllocation* FindLarge(const void* pointer) noexcept {
        for (LargeAllocation& block : largeAllocations)
            if (block.handle && block.base == (unsigned)pointer) return &block;
        return nullptr;
    }
    void Initialize(Block* block, unsigned capacity, unsigned previous, unsigned next) noexcept {
        ::new ((void*)block) Block{next, previous, capacity, 0, 0, Alignment, 0, Magic};
    }
    void Merge(Block* left, Block* right) noexcept {
        left->capacity += Header + right->capacity;
        left->next = right->next;
        if (left->next) At(left->next)->prev = Address(left);
        right->~Block();
    }
    void Split(Block* block, unsigned needed) noexcept {
        if (block->capacity - needed < Header + Minimum) return;
        const unsigned next = block->next;
        Block* tail = At(Address(block) + Header + needed);
        Initialize(tail, block->capacity - needed - Header, Address(block), next);
        block->capacity = needed;
        block->next = Address(tail);
        if (next) {
            At(next)->prev = Address(tail);
            if (!At(next)->allocated) Merge(tail, At(next));
        }
    }
    void Release(unsigned handle) noexcept {
        const GtosVmControlRequest request = {GTOS_VM_ABI_VERSION, handle, 0};
        if (Call(GTOS_SYS_VM_RELEASE, (unsigned)&request, sizeof(request)) != 0)
            gtos_native_heap_panic(GTOS_NATIVE_HEAP_EXIT_RELEASE);
    }
    bool EnsureArena(Arena& arena) noexcept {
        if (arena.handle) return true;
        GtosVmReserveResult result = {};
        const GtosVmReserveRequest reserve = {GTOS_VM_ABI_VERSION, GTOS_NATIVE_HEAP_BYTES,
            GTOS_VM_PAGE_BYTES, 0, (unsigned)&result};
        if (Call(GTOS_SYS_VM_RESERVE, (unsigned)&reserve, sizeof(reserve)) != 0) return false;
        if (result.version != GTOS_VM_ABI_VERSION || !result.handle || result.handle > 0x7FFFFFFFU
            || result.base < ArenaBase || result.base > ArenaLimit - GTOS_NATIVE_HEAP_BYTES
            || (result.base & (GTOS_VM_PAGE_BYTES - 1U)) || result.length != GTOS_NATIVE_HEAP_BYTES
            || result.page_size != GTOS_VM_PAGE_BYTES)
            gtos_native_heap_panic(GTOS_NATIVE_HEAP_EXIT_VM_CONTRACT);
        const GtosVmRangeRequest commit = {GTOS_VM_ABI_VERSION, result.handle, 0,
            GTOS_NATIVE_HEAP_BYTES, GTOS_VM_READ_WRITE};
        if (Call(GTOS_SYS_VM_SET_PERMISSIONS, (unsigned)&commit, sizeof(commit)) != 0) {
            Release(result.handle);
            return false;
        }
        Initialize(At(result.base), MaximumPayload, 0, 0);
        arena = {result.base, result.handle, result.length, result.base, 0};
        return true;
    }
    void* AllocateLarge(size_t bytes, unsigned alignment) noexcept {
        if (!largeAllocationsEnabled || !bytes || bytes > GTOS_NATIVE_HEAP_LARGE_BYTES)
            return nullptr;
        LargeAllocation* slot = nullptr;
        for (LargeAllocation& block : largeAllocations)
            if (!block.handle) { slot = &block; break; }
        if (!slot) return nullptr;
        // The size bound above makes rounding safe. Reservation alignment
        // makes the whole mapping a payload; ownership stays out of user data.
        const unsigned length = ((unsigned)bytes + GTOS_VM_PAGE_BYTES - 1U)
            & ~(GTOS_VM_PAGE_BYTES - 1U);
        const unsigned pageAlignment = alignment < GTOS_VM_PAGE_BYTES ? GTOS_VM_PAGE_BYTES : alignment;
        GtosVmReserveResult result = {};
        const GtosVmReserveRequest reserve = {GTOS_VM_ABI_VERSION, length, pageAlignment, 0, (unsigned)&result};
        if (Call(GTOS_SYS_VM_RESERVE, (unsigned)&reserve, sizeof(reserve)) != 0) return nullptr;
        if (result.version != GTOS_VM_ABI_VERSION || !result.handle || result.handle > 0x7FFFFFFFU
            || result.base < ArenaBase || result.base > ArenaLimit - length
            || (result.base & (pageAlignment - 1U)) || result.length != length
            || result.page_size != GTOS_VM_PAGE_BYTES)
            gtos_native_heap_panic(GTOS_NATIVE_HEAP_EXIT_VM_CONTRACT);
        const GtosVmRangeRequest commit = {GTOS_VM_ABI_VERSION, result.handle, 0, length, GTOS_VM_READ_WRITE};
        if (Call(GTOS_SYS_VM_SET_PERMISSIONS, (unsigned)&commit, sizeof(commit)) != 0) {
            Release(result.handle);
            return nullptr;
        }
        *slot = {result.base, result.handle, result.length, (unsigned)bytes, alignment};
        return (void*)result.base;
    }
    void* Allocate(Arena& arena, size_t bytes, unsigned alignment) noexcept {
        if (!bytes) return nullptr;
        if (bytes > MaximumPayload) {
            return nullptr;
        }
        Validate(arena);
        if (!EnsureArena(arena)) {
            return nullptr;
        }
        for (unsigned address = arena.first; address; address = At(address)->next) {
            Block* block = At(address);
            if (block->allocated) continue;
            const unsigned offset = (0U - (address + Header)) & (alignment - 1U);
            if (offset > block->capacity || bytes > block->capacity - offset) continue;
            const unsigned needed = (offset + (unsigned)bytes + Alignment - 1U) & ~(Alignment - 1U);
            Split(block, needed);
            block->requested = (unsigned)bytes;
            block->userOffset = offset;
            block->alignment = alignment;
            block->allocated = 1;
            ++arena.liveBlocks;
            Validate(arena);
            return (void*)Payload(block);
        }
        // A newly initialized arena that cannot satisfy alignment must not
        // become an idle resident cache after an unsuccessful allocation.
        if (!arena.liveBlocks) {
            Release(arena.handle);
            arena = {};
        }
        return nullptr;
    }
    void* Acquire(size_t bytes, unsigned alignment) noexcept {
        if (!bytes) return nullptr;
        ValidateAll();
        void* result = Allocate(arena, bytes, alignment);
        if (!result && largeAllocationsEnabled) {
            // Pack small overflow requests; one descriptor/page per tiny
            // allocation exhausts region slots before V8 can deserialize.
            for (Arena& owner : extraArenas)
                if (owner.handle && !result) result = Allocate(owner, bytes, alignment);
            if (!result && bytes <= GTOS_NATIVE_HEAP_SMALL_FALLBACK_BYTES
                && alignment <= GTOS_VM_PAGE_BYTES) {
                for (Arena& owner : extraArenas) if (!owner.handle) {
                    result = Allocate(owner, bytes, alignment);
                    break;
                }
            } else if (!result) result = AllocateLarge(bytes, alignment);
        }
        ValidateAll();
        return result;
    }
    Block* Find(const Arena& arena, const void* pointer) noexcept {
        for (unsigned address = arena.first; address; address = At(address)->next) {
            Block* block = At(address);
            if (block->allocated && Payload(block) == (unsigned)pointer) return block;
        }
        gtos_native_heap_panic(GTOS_NATIVE_HEAP_EXIT_BAD_POINTER);
    }
    void FreeBlock(Arena& arena, Block* block) noexcept {
        block->requested = block->userOffset = block->allocated = 0;
        block->alignment = Alignment;
        --arena.liveBlocks;
        if (block->prev && !At(block->prev)->allocated) {
            Block* previous = At(block->prev);
            Merge(previous, block);
            block = previous;
        }
        if (block->next && !At(block->next)->allocated) Merge(block, At(block->next));
        Validate(arena);
        if (!arena.liveBlocks) {
            Release(arena.handle);
            arena = {};
        }
    }
    void FreePointer(void* pointer) noexcept {
        if (LargeAllocation* block = FindLarge(pointer)) {
            Release(block->handle);
            *block = {};
        } else {
            Arena& owner = FindArena(pointer);
            FreeBlock(owner, Find(owner, pointer));
        }
    }
    bool OutputAddress(const void* output) noexcept {
        const unsigned address = (unsigned)output;
        return address >= UserBase && address <= UserLimit - sizeof(void*)
            && !(address & (alignof(void*) - 1U));
    }
}
extern "C" int gtos_native_heap_enable_large_allocations() noexcept {
    HeapGuard guard;
    ValidateAll();
    for (unsigned i = 0; i < GTOS_NATIVE_HEAP_ARENAS; ++i)
        if (ArenaAt(i).handle) return EBUSY;
    for (const LargeAllocation& block : largeAllocations) if (block.handle) return EBUSY;
    largeAllocationsEnabled = 1;
    return 0;
}
extern "C" [[noreturn]] void gtos_native_heap_panic(unsigned code) noexcept {
    static const char failure[] = "GTOS NATIVE HEAP FAIL V1\n";
    Call(GTOS_SYS_WRITE, (unsigned)failure, sizeof(failure) - 1);
    Call(GTOS_SYS_EXIT, GTOS_NATIVE_HEAP_EXIT_BASE | code, 0);
    asm volatile("ud2");
    __builtin_unreachable();
}
extern "C" void* malloc(size_t bytes) noexcept {
    HeapGuard guard;
    void* result = Acquire(bytes, Alignment);
    if (!result && bytes) errno = ENOMEM;
    return result;
}
extern "C" void* calloc(size_t count, size_t bytes) noexcept {
    if (!count || !bytes) return nullptr;
    if (count > (~(size_t)0) / bytes) {
        errno = ENOMEM;
        return nullptr;
    }
    const size_t length = count * bytes;
    void* result = malloc(length);
    if (result) {
        volatile unsigned char* output = (volatile unsigned char*)result;
        for (size_t i = 0; i < length; ++i) output[i] = 0;
    }
    return result;
}
extern "C" void free(void* pointer) noexcept {
    if (!pointer) return;
    HeapGuard guard;
    ValidateAll();
    FreePointer(pointer);
    ValidateAll();
}
extern "C" void* realloc(void* pointer, size_t bytes) noexcept {
    if (!pointer) return malloc(bytes);
    HeapGuard guard;
    ValidateAll();
    if (LargeAllocation* large = FindLarge(pointer)) {
        if (!bytes) { FreePointer(pointer); return nullptr; }
        if (bytes <= large->length) { large->requested = (unsigned)bytes; return pointer; }
        void* replacement = Acquire(bytes, large->alignment);
        if (!replacement) { errno = ENOMEM; return nullptr; }
        const unsigned length = large->requested;
        volatile unsigned char* output = (volatile unsigned char*)replacement;
        const volatile unsigned char* input = (const volatile unsigned char*)pointer;
        for (unsigned i = 0; i < length; ++i) output[i] = input[i];
        FreePointer(pointer);
        ValidateAll();
        return replacement;
    }
    Arena& owner = FindArena(pointer);
    Block* block = Find(owner, pointer);
    if (!bytes) {
        FreeBlock(owner, block);
        return nullptr;
    }
    if (bytes > MaximumPayload && !largeAllocationsEnabled) {
        errno = ENOMEM;
        return nullptr;
    }
    unsigned needed = GTOS_NATIVE_HEAP_BYTES;
    if (bytes <= MaximumPayload - block->userOffset)
        needed = (block->userOffset + (unsigned)bytes + Alignment - 1U) & ~(Alignment - 1U);
    if (needed <= block->capacity) {
        Split(block, needed);
        block->requested = (unsigned)bytes;
        ValidateAll();
        return pointer;
    }
    if (block->next && !At(block->next)->allocated
        && needed <= block->capacity + Header + At(block->next)->capacity) {
        Merge(block, At(block->next));
        Split(block, needed);
        block->requested = (unsigned)bytes;
        ValidateAll();
        return pointer;
    }
    void* replacement = Acquire(bytes, block->alignment);
    if (!replacement) {
        errno = ENOMEM;
        return nullptr;
    }
    const unsigned length = block->requested < bytes ? block->requested : (unsigned)bytes;
    volatile unsigned char* output = (volatile unsigned char*)replacement;
    const volatile unsigned char* input = (const volatile unsigned char*)pointer;
    for (unsigned i = 0; i < length; ++i) output[i] = input[i];
    FreePointer(pointer);
    ValidateAll();
    return replacement;
}
extern "C" int posix_memalign(void** output, size_t alignment, size_t bytes) noexcept {
    if (!PowerOfTwo((unsigned)alignment) || alignment < sizeof(void*) || !OutputAddress(output)) return EINVAL;
    if (alignment < Alignment) alignment = Alignment;
    HeapGuard guard;
    void* result = Acquire(bytes, (unsigned)alignment);
    if (!result && bytes) return ENOMEM;
    *output = result;
    return 0;
}
extern "C" int gtos_native_heap_query(GtosVmRegionInfo* output) noexcept {
    HeapGuard guard;
    ValidateAll();
    if (!arena.handle) return 0;
    const GtosVmQueryRequest request = {GTOS_VM_ABI_VERSION, arena.handle, (unsigned)output};
    if (Call(GTOS_SYS_VM_QUERY, (unsigned)&request, sizeof(request)) != 0) return 0;
    if (output->version != GTOS_VM_ABI_VERSION || output->handle != arena.handle
        || output->base != arena.base || output->length != arena.length
        || output->resident_pages != GTOS_NATIVE_HEAP_BYTES / GTOS_VM_PAGE_BYTES)
        gtos_native_heap_panic(GTOS_NATIVE_HEAP_EXIT_VM_CONTRACT);
    return 1;
}
