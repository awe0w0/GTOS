#include <stddef.h>
#include "src/base/platform/memory.h"

#if !defined(__GTOS__) || !V8_OS_GTOS || V8_OS_LINUX || V8_OS_POSIX || defined(__linux__) || defined(__unix__) || defined(_WIN32)
#error This consumer must compile for the actual freestanding GTOS i386 target
#endif
static_assert(sizeof(void*)==4 && sizeof(size_t)==4, "GTOS heap consumer is IA32");

// Compile each actual upstream inline operation with runtime arguments. The
// guest main checks the resulting allocations; these functions add no policy.
extern "C" [[gnu::noinline]] void* actual_v8_malloc(size_t size) {
    return v8::base::Malloc(size);
}
extern "C" [[gnu::noinline]] void* actual_v8_calloc(size_t count,size_t size) {
    return v8::base::Calloc(count,size);
}
extern "C" [[gnu::noinline]] void* actual_v8_realloc(void* memory,size_t size) {
    return v8::base::Realloc(memory,size);
}
extern "C" [[gnu::noinline]] void actual_v8_free(void* memory) {
    v8::base::Free(memory);
}
extern "C" [[gnu::noinline]] void* actual_v8_aligned_alloc(size_t size,size_t alignment) {
    return v8::base::AlignedAlloc(size,alignment);
}
extern "C" [[gnu::noinline]] void actual_v8_aligned_free(void* memory) {
    v8::base::AlignedFree(memory);
}
extern "C" [[gnu::noinline]] void* actual_v8_allocate_at_least(size_t count,size_t* actual_count) {
    const auto result=v8::base::AllocateAtLeast<unsigned char>(count);
    *actual_count=result.count;
    return result.ptr;
}
