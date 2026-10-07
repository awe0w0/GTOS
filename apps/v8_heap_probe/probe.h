#ifndef GTOS_V8_HEAP_PROBE_H
#define GTOS_V8_HEAP_PROBE_H
#include <stddef.h>
#include "heap.h"
struct HeapProbeRecord {
    unsigned version,mode,stage,base,handle,primary,primaryBytes,neighbor,neighborBytes,checks;
};
static_assert(sizeof(HeapProbeRecord)==40,"Heap guest record wire");
extern "C" volatile HeapProbeRecord heap_probe_record;
extern "C" int heap_probe_call(unsigned operation,unsigned first,unsigned second);
extern "C" void* actual_v8_malloc(size_t);
extern "C" void* actual_v8_calloc(size_t,size_t);
extern "C" void* actual_v8_realloc(void*,size_t);
extern "C" void actual_v8_free(void*);
extern "C" void* actual_v8_aligned_alloc(size_t,size_t);
extern "C" void actual_v8_aligned_free(void*);
extern "C" void* actual_v8_allocate_at_least(size_t,size_t*);
#define HEAP_CPP_ALLOCATIONS(X) \
    X(scalar) X(array) X(nothrow_scalar) X(nothrow_array) \
    X(aligned_scalar) X(aligned_array) X(aligned_nothrow_scalar) X(aligned_nothrow_array)
#define HEAP_CPP_DEALLOCATIONS(X) \
    X(scalar) X(array) X(nothrow_scalar) X(nothrow_array) X(sized_scalar) X(sized_array) \
    X(aligned_scalar) X(aligned_array) X(aligned_nothrow_scalar) X(aligned_nothrow_array) \
    X(sized_aligned_scalar) X(sized_aligned_array)
#define DECLARE_ALLOC(name) extern "C" void* actual_cpp_new_##name(size_t);
HEAP_CPP_ALLOCATIONS(DECLARE_ALLOC)
#undef DECLARE_ALLOC
#define DECLARE_FREE(name) extern "C" void actual_cpp_delete_##name(void*,size_t);
HEAP_CPP_DEALLOCATIONS(DECLARE_FREE)
#undef DECLARE_FREE
extern "C" void* actual_cpp_construct();
extern "C" void* actual_cpp_construct_nothrow_oversized();
extern "C" void actual_cpp_destroy(void*);
extern "C" unsigned actual_cpp_constructed();
extern "C" unsigned actual_cpp_destroyed();
#endif
