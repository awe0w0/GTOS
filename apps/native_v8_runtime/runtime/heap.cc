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
    int heap_errno;

    int Call(unsigned operation, unsigned first, unsigned second) noexcept {
        int result;
        asm volatile("int $0x80" : "=a"(result)
            : "a"(operation), "b"(first), "c"(second) : "memory", "cc");
        return result;
    }
    Block* At(unsigned address) noexcept { return (Block*)address; }
    unsigned Address(const Block* block) noexcept { return (unsigned)block; }
    unsigned Payload(const Block* block) noexcept { return Address(block) + Header + block->userOffset; }
    bool PowerOfTwo(unsigned value) noexcept { return value && !(value & (value - 1U)); }
    [[noreturn]] void Corrupt() noexcept { gtos_native_heap_panic(GTOS_NATIVE_HEAP_EXIT_CORRUPT); }
    void Validate() noexcept {
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
    bool EnsureArena() noexcept {
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
    void* Allocate(size_t bytes, unsigned alignment, bool reportError) noexcept {
        if (!bytes) return nullptr;
        if (bytes > MaximumPayload) {
            if (reportError) heap_errno = ENOMEM;
            return nullptr;
        }
        Validate();
        if (!EnsureArena()) {
            if (reportError) heap_errno = ENOMEM;
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
            Validate();
            return (void*)Payload(block);
        }
        // A newly initialized arena that cannot satisfy alignment must not
        // become an idle resident cache after an unsuccessful allocation.
        if (!arena.liveBlocks) {
            Release(arena.handle);
            arena = {};
        }
        if (reportError) heap_errno = ENOMEM;
        return nullptr;
    }
    Block* Find(const void* pointer) noexcept {
        for (unsigned address = arena.first; address; address = At(address)->next) {
            Block* block = At(address);
            if (block->allocated && Payload(block) == (unsigned)pointer) return block;
        }
        gtos_native_heap_panic(GTOS_NATIVE_HEAP_EXIT_BAD_POINTER);
    }
    void FreeBlock(Block* block) noexcept {
        block->requested = block->userOffset = block->allocated = 0;
        block->alignment = Alignment;
        --arena.liveBlocks;
        if (block->prev && !At(block->prev)->allocated) {
            Block* previous = At(block->prev);
            Merge(previous, block);
            block = previous;
        }
        if (block->next && !At(block->next)->allocated) Merge(block, At(block->next));
        Validate();
        if (!arena.liveBlocks) {
            Release(arena.handle);
            arena = {};
        }
    }
    bool OutputAddress(const void* output) noexcept {
        const unsigned address = (unsigned)output;
        return address >= UserBase && address <= UserLimit - sizeof(void*)
            && !(address & (alignof(void*) - 1U));
    }
}
extern "C" [[noreturn]] void gtos_native_heap_panic(unsigned code) noexcept {
    static const char failure[] = "GTOS NATIVE HEAP FAIL V1\n";
    Call(GTOS_SYS_WRITE, (unsigned)failure, sizeof(failure) - 1);
    Call(GTOS_SYS_EXIT, GTOS_NATIVE_HEAP_EXIT_BASE | code, 0);
    asm volatile("ud2");
    __builtin_unreachable();
}
extern "C" int* __llvm_libc_errno(void) noexcept { return &heap_errno; }
extern "C" void* malloc(size_t bytes) noexcept { return Allocate(bytes, Alignment, true); }
extern "C" void* calloc(size_t count, size_t bytes) noexcept {
    if (!count || !bytes) return nullptr;
    if (count > (~(size_t)0) / bytes) {
        heap_errno = ENOMEM;
        return nullptr;
    }
    const size_t length = count * bytes;
    void* result = Allocate(length, Alignment, true);
    if (result) {
        volatile unsigned char* output = (volatile unsigned char*)result;
        for (size_t i = 0; i < length; ++i) output[i] = 0;
    }
    return result;
}
extern "C" void free(void* pointer) noexcept {
    if (!pointer) return;
    Validate();
    FreeBlock(Find(pointer));
}
extern "C" void* realloc(void* pointer, size_t bytes) noexcept {
    if (!pointer) return malloc(bytes);
    Validate();
    Block* block = Find(pointer);
    if (!bytes) {
        FreeBlock(block);
        return nullptr;
    }
    if (bytes > MaximumPayload) {
        heap_errno = ENOMEM;
        return nullptr;
    }
    unsigned needed = GTOS_NATIVE_HEAP_BYTES;
    if (bytes <= MaximumPayload - block->userOffset)
        needed = (block->userOffset + (unsigned)bytes + Alignment - 1U) & ~(Alignment - 1U);
    if (needed <= block->capacity) {
        Split(block, needed);
        block->requested = (unsigned)bytes;
        Validate();
        return pointer;
    }
    if (block->next && !At(block->next)->allocated
        && needed <= block->capacity + Header + At(block->next)->capacity) {
        Merge(block, At(block->next));
        Split(block, needed);
        block->requested = (unsigned)bytes;
        Validate();
        return pointer;
    }
    void* replacement = Allocate(bytes, block->alignment, true);
    if (!replacement) return nullptr;
    const unsigned length = block->requested < bytes ? block->requested : (unsigned)bytes;
    volatile unsigned char* output = (volatile unsigned char*)replacement;
    const volatile unsigned char* input = (const volatile unsigned char*)pointer;
    for (unsigned i = 0; i < length; ++i) output[i] = input[i];
    free(pointer);
    return replacement;
}
extern "C" int posix_memalign(void** output, size_t alignment, size_t bytes) noexcept {
    if (!PowerOfTwo((unsigned)alignment) || alignment < sizeof(void*) || !OutputAddress(output)) return EINVAL;
    if (alignment < Alignment) alignment = Alignment;
    void* result = Allocate(bytes, (unsigned)alignment, false);
    if (!result && bytes) return ENOMEM;
    *output = result;
    return 0;
}
extern "C" int gtos_native_heap_query(GtosVmRegionInfo* output) noexcept {
    Validate();
    if (!arena.handle) return 0;
    const GtosVmQueryRequest request = {GTOS_VM_ABI_VERSION, arena.handle, (unsigned)output};
    if (Call(GTOS_SYS_VM_QUERY, (unsigned)&request, sizeof(request)) != 0) return 0;
    if (output->version != GTOS_VM_ABI_VERSION || output->handle != arena.handle
        || output->base != arena.base || output->length != arena.length
        || output->resident_pages != GTOS_NATIVE_HEAP_BYTES / GTOS_VM_PAGE_BYTES)
        gtos_native_heap_panic(GTOS_NATIVE_HEAP_EXIT_VM_CONTRACT);
    return 1;
}