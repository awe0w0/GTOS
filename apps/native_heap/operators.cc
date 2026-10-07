#include "heap.h"
#include <stdlib.h>
#include <new>

namespace {
    void* New(size_t bytes, size_t alignment, bool nothrow) noexcept {
        if (!bytes) bytes = 1;
        void* result = nullptr;
        if (alignment <= GTOS_NATIVE_HEAP_ALIGNMENT) result = malloc(bytes);
        else if (posix_memalign(&result, alignment, bytes) != 0) result = nullptr;
        if (!result && !nothrow) gtos_native_heap_panic(GTOS_NATIVE_HEAP_EXIT_NEW_OOM);
        return result;
    }
}
namespace std { const nothrow_t nothrow{}; }
void* operator new(size_t bytes) { return New(bytes, GTOS_NATIVE_HEAP_ALIGNMENT, false); }
void* operator new[](size_t bytes) { return New(bytes, GTOS_NATIVE_HEAP_ALIGNMENT, false); }
void* operator new(size_t bytes, const std::nothrow_t&) noexcept { return New(bytes, GTOS_NATIVE_HEAP_ALIGNMENT, true); }
void* operator new[](size_t bytes, const std::nothrow_t&) noexcept { return New(bytes, GTOS_NATIVE_HEAP_ALIGNMENT, true); }
void operator delete(void* pointer) noexcept { free(pointer); }
void operator delete[](void* pointer) noexcept { free(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { free(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { free(pointer); }
void operator delete(void* pointer, size_t) noexcept { free(pointer); }
void operator delete[](void* pointer, size_t) noexcept { free(pointer); }
#if defined(__cpp_aligned_new)
void* operator new(size_t bytes, std::align_val_t alignment) { return New(bytes, (size_t)alignment, false); }
void* operator new[](size_t bytes, std::align_val_t alignment) { return New(bytes, (size_t)alignment, false); }
void* operator new(size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept { return New(bytes, (size_t)alignment, true); }
void* operator new[](size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept { return New(bytes, (size_t)alignment, true); }
void operator delete(void* pointer, std::align_val_t) noexcept { free(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { free(pointer); }
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { free(pointer); }
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { free(pointer); }
void operator delete(void* pointer, size_t, std::align_val_t) noexcept { free(pointer); }
void operator delete[](void* pointer, size_t, std::align_val_t) noexcept { free(pointer); }
#endif