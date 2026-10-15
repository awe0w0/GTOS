#ifndef GTOS_NATIVE_HEAP_H
#define GTOS_NATIVE_HEAP_H
#include <process/vm_abi.h>

#define GTOS_NATIVE_HEAP_BYTES 65536U
#define GTOS_NATIVE_HEAP_ALIGNMENT 16U
#define GTOS_NATIVE_HEAP_HEADER_BYTES 32U
#define GTOS_NATIVE_HEAP_LARGE_SLOTS 16U
#define GTOS_NATIVE_HEAP_ARENAS 8U
#define GTOS_NATIVE_HEAP_SMALL_FALLBACK_BYTES 16384U
#define GTOS_NATIVE_HEAP_LARGE_BYTES (256U * GTOS_VM_PAGE_BYTES)
#define GTOS_NATIVE_HEAP_EXIT_BASE 0x48000000U
#define GTOS_NATIVE_HEAP_EXIT_CORRUPT 1U
#define GTOS_NATIVE_HEAP_EXIT_BAD_POINTER 2U
#define GTOS_NATIVE_HEAP_EXIT_VM_CONTRACT 3U
#define GTOS_NATIVE_HEAP_EXIT_RELEASE 4U
#define GTOS_NATIVE_HEAP_EXIT_NEW_OOM 0x10U

// The standard allocation declarations come from the genuine SDK stdlib.h
// and new headers. The default heap has one arena per process. Explicit V8
// opt-in adds up to seven lazy arenas for small requests and independent large
// mappings. Empty arenas are released, including the primary arena. Metadata
// is serialized with an atomic lock and real Yield on the BSP scheduler.
// No async signal/allocator callback or independently killed worker is supported;
// the native fault/cancel path terminates the whole shared process.
// Zero-size malloc/calloc return null without changing errno;
// realloc(p,0) frees p. Positive allocation failure preserves existing blocks.
// posix_memalign rejects a null, misaligned or out-of-user-range output before
// writing it. Other outputs must be valid writable pointer objects: this
// user library cannot prevalidate arbitrary mapped-page permissions.
// Pointers not matching a current live payload terminate before mutation.
// Valid free/realloc pointers remain a caller precondition: a stale pointer
// whose address has been reallocated cannot be distinguished from its owner.
// Ordinary new terminates on OOM in the native no-exception profile; explicit
// nothrow new returns null. No exception/new_handler runtime is advertised.
extern "C" {
    // Explicit whole-V8 opt-in, before the first live allocation. Returns
    // EBUSY if the small arena or a separate allocation is live; no errno write.
    // Fallback mappings use the same process VM quota, never a larger quota.
    int gtos_native_heap_enable_large_allocations() noexcept;
    // The live result is obtained from the real kernel VM_QUERY. Empty heaps
    // and failed kernel output prevalidation return 0 without writing output.
    // This diagnostic describes the original small arena only. Opt-in separate
    // mappings are individually owned and are not represented by this result.
    int gtos_native_heap_query(GtosVmRegionInfo* output) noexcept;
    [[noreturn]] void gtos_native_heap_panic(unsigned code) noexcept;
}
#endif
