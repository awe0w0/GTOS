// Genuine C allocation declarations from the qualified target SDK. Every call
// uses the production native heap and real int80 VM; no host allocator shim.
#include "record.h"
#include "../native_heap/heap.h"
#include <process/abi.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#if !defined(__i386__) || !defined(__GTOS__) || defined(__linux__) || defined(__unix__) || defined(_WIN32)
#error The raw heap probe requires the real GTOS i386 ABI and qualified C headers
#endif
static_assert(sizeof(void*) == 4 && sizeof(size_t) == 4, "Raw GCC IA32 target ABI");
#ifndef GTOS_HEAP_PROBE_MODE
#define GTOS_HEAP_PROBE_MODE 0
#endif
#if GTOS_HEAP_PROBE_MODE < 0 || GTOS_HEAP_PROBE_MODE > 7
#error Unknown heap probe mode
#endif
extern "C" {
    volatile HeapProbeRecord native_heap_record __attribute__((section(".data.heap_record"), used, aligned(4)))
        = {1, GTOS_HEAP_PROBE_MODE, 0, 0, 0, 0, 0, 0, 0, 0};
}
namespace {
    unsigned Call(unsigned operation, unsigned first = 0, unsigned second = 0) {
        unsigned result;
        asm volatile("int $0x80" : "=a"(result)
            : "a"(operation), "b"(first), "c"(second) : "memory", "cc");
        return result;
    }
    [[noreturn]] void Exit(unsigned code) {
        Call(GTOS_SYS_EXIT, code);
        for (;;) asm volatile("ud2");
    }
    [[noreturn]] void Fail(unsigned line) {
        native_heap_record.stage = 0x80000000U | line;
        static const char text[] = "GTOS HEAP PROBE FAIL V1\n";
        Call(GTOS_SYS_WRITE, (unsigned)text, sizeof(text) - 1);
        Exit(line);
    }
#define REQUIRE(value) do { if (!(value)) Fail(__LINE__); native_heap_record.checks = native_heap_record.checks + 1U; } while (0)
    void Fill(void* pointer, unsigned bytes, unsigned seed) {
        volatile unsigned char* output = (volatile unsigned char*)pointer;
        for (unsigned i = 0; i < bytes; ++i) output[i] = (unsigned char)(i ^ seed);
    }
    inline bool Pattern(const void* pointer, unsigned bytes, unsigned seed) {
        const volatile unsigned char* input = (const volatile unsigned char*)pointer;
        for (unsigned i = 0; i < bytes; ++i) if (input[i] != (unsigned char)(i ^ seed)) return false;
        return true;
    }
    inline bool Zero(const void* pointer, unsigned bytes) {
        const volatile unsigned char* input = (const volatile unsigned char*)pointer;
        for (unsigned i = 0; i < bytes; ++i) if (input[i]) return false;
        return true;
    }
    inline void Sentinel(void* pointer, unsigned bytes) {
        volatile unsigned char* output = (volatile unsigned char*)pointer;
        for (unsigned i = 0; i < bytes; ++i) output[i] = 0xA5;
    }
    inline bool Unchanged(const void* pointer, unsigned bytes) {
        const volatile unsigned char* input = (const volatile unsigned char*)pointer;
        for (unsigned i = 0; i < bytes; ++i) if (input[i] != 0xA5) return false;
        return true;
    }
    GtosVmRegionInfo Query() {
        GtosVmRegionInfo info = {};
        REQUIRE(gtos_native_heap_query(&info) == 1);
        REQUIRE(info.version == GTOS_VM_ABI_VERSION && info.handle && info.handle <= 0x7FFFFFFFU
            && info.base >= 0x80000000U && info.base <= 0xBFFFC000U - 65536U
            && !(info.base & 4095U) && info.length == 65536U && info.resident_pages == 16);
        return info;
    }
    inline void Empty() {
        GtosVmRegionInfo output;
        Sentinel(&output, sizeof(output));
        REQUIRE(gtos_native_heap_query(&output) == 0 && Unchanged(&output, sizeof(output)));
    }
    inline int Wire(unsigned operation, const void* request, unsigned bytes) {
        return (int)Call(operation, (unsigned)request, bytes);
    }
#if GTOS_HEAP_PROBE_MODE == 0
    void FirstCommitFailure() {
        // Consume the real per-process resident budget through the public VM
        // ABI, discovering its boundary rather than copying ELF page totals.
        GtosVmReserveResult spare = {};
        const GtosVmReserveRequest reserve = {1, 256U * 4096U, 4096, 0, (unsigned)&spare};
        REQUIRE(Wire(GTOS_SYS_VM_RESERVE, &reserve, sizeof(reserve)) == 0);
        unsigned committed = 0;
        for (; committed < 256; ++committed) {
            const GtosVmRangeRequest page = {1, spare.handle, committed * 4096U, 4096, GTOS_VM_READ_WRITE};
            const int result = Wire(GTOS_SYS_VM_SET_PERMISSIONS, &page, sizeof(page));
            if (result == GTOS_VM_ERR_LIMIT) break;
            REQUIRE(result == 0);
        }
        REQUIRE(committed > 200 && committed < 256);
        Fill((void*)spare.base, committed * 4096U, 0xD3);
        for (unsigned attempt = 0; attempt < 40; ++attempt) {
            errno = 1976;
            REQUIRE(malloc(16) == nullptr && errno == ENOMEM);
            Empty();
        }
        GtosVmRegionInfo after = {};
        const GtosVmQueryRequest query = {1, spare.handle, (unsigned)&after};
        REQUIRE(Wire(GTOS_SYS_VM_QUERY, &query, sizeof(query)) == 0
            && after.resident_pages == committed && after.base == spare.base);
        REQUIRE(Pattern((void*)spare.base, committed * 4096U, 0xD3));
        GtosVmReserveResult capacity = {};
        const GtosVmReserveRequest available = {1, 4096, 4096, 0, (unsigned)&capacity};
        REQUIRE(Wire(GTOS_SYS_VM_RESERVE, &available, sizeof(available)) == 0);
        const GtosVmControlRequest removeCapacity = {1, capacity.handle, 0};
        REQUIRE(Wire(GTOS_SYS_VM_RELEASE, &removeCapacity, sizeof(removeCapacity)) == 0);
        const GtosVmControlRequest release = {1, spare.handle, 0};
        REQUIRE(Wire(GTOS_SYS_VM_RELEASE, &release, sizeof(release)) == 0);
        void* fresh = malloc(4096);
        REQUIRE(fresh && Zero(fresh, 4096));
        free(fresh); Empty();
    }
    void Positive() {
        volatile size_t huge = ~(size_t)0;
        Empty(); errno = 1976;
        REQUIRE(malloc(0) == nullptr && calloc(0, huge) == nullptr && calloc(huge, 0) == nullptr
            && realloc(nullptr, 0) == nullptr && errno == 1976);
        free(nullptr); Empty();
        REQUIRE(malloc(65536) == nullptr && errno == ENOMEM); Empty();
        errno = 1976;
        REQUIRE(calloc(huge, 2) == nullptr && errno == ENOMEM); Empty();
        void* output = (void*)0x12345670U;
        const size_t invalid[] = {0, 1, 2, 3, 12, huge};
        for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
            errno = 1976;
            REQUIRE(posix_memalign(&output, invalid[i], 16) == EINVAL
                && output == (void*)0x12345670U && errno == 1976);
        }
        errno = 1976;
        REQUIRE(posix_memalign(nullptr, 16, 16) == EINVAL && errno == 1976);
        alignas(4) unsigned char badOutput[12]; Sentinel(badOutput, sizeof(badOutput));
        REQUIRE(posix_memalign((void**)(badOutput + 1), 16, 16) == EINVAL
            && Unchanged(badOutput, sizeof(badOutput)) && errno == 1976);
        REQUIRE(posix_memalign((void**)0xBFFFFFFFU, 16, 16) == EINVAL && errno == 1976);
        REQUIRE(posix_memalign((void**)0xFFFFFFFCU, 16, 16) == EINVAL && errno == 1976);
        REQUIRE(posix_memalign(&output, 16, 0) == 0 && !output && errno == 1976); Empty();
        FirstCommitFailure();
        void* whole = malloc(65504);
        REQUIRE(whole && !((unsigned)whole & 15U)); Fill(whole, 65504, 0xDA);
        const GtosVmRegionInfo full = Query();
        REQUIRE(malloc(1) == nullptr && errno == ENOMEM);
        REQUIRE(realloc(whole, 65505) == nullptr && Pattern(whole, 65504, 0xDA));
        errno = 1976; output = (void*)0x12345670U;
        REQUIRE(posix_memalign(&output, 64, 1) == ENOMEM && output == (void*)0x12345670U && errno == 1976);
        const GtosVmRegionInfo preserved = Query();
        REQUIRE(preserved.handle == full.handle && preserved.base == full.base); free(whole); Empty();
        GtosVmRegionInfo stale; Sentinel(&stale, sizeof(stale));
        const GtosVmQueryRequest staleQuery = {1, full.handle, (unsigned)&stale};
        REQUIRE(Wire(GTOS_SYS_VM_QUERY, &staleQuery, sizeof(staleQuery)) == GTOS_VM_ERR_BAD_STATE
            && Unchanged(&stale, sizeof(stale)));
        void* a = malloc(8192); void* b = malloc(8192); void* c = malloc(1024);
        REQUIRE(a && b && c); Fill(a, 8192, 0x19); Fill(b, 8192, 0x29); Fill(c, 1024, 0x39);
        free(b); free(a);
        void* joined = malloc(16384);
        REQUIRE(joined == a && Pattern(c, 1024, 0x39)); Fill(joined, 16384, 0x49);
        REQUIRE(Pattern(c, 1024, 0x39)); free(joined); free(c); Empty();
        a = malloc(1024); b = malloc(4096);
        REQUIRE(a && b); Fill(a, 1024, 0xAB); Fill(b, 4096, 0xCD);
        const unsigned dirtyAddress = (unsigned)b;
        free(b); b = calloc(4096, 1);
        REQUIRE((unsigned)b == dirtyAddress && Zero(b, 4096) && Pattern(a, 1024, 0xAB));
        free(a); free(b); Empty();
        // A too-small adjacent free block must survive a failed growth intact.
        a = malloc(1024); b = malloc(128); REQUIRE(a && b);
        Fill(a, 1024, 0xAD); const unsigned holeAddress = (unsigned)b;
        void* neighbors[256]; unsigned neighborsCount = 0;
        for (; neighborsCount < 256; ++neighborsCount) {
            neighbors[neighborsCount] = malloc(256);
            if (!neighbors[neighborsCount]) break;
            Fill(neighbors[neighborsCount], 256, neighborsCount ^ 0xCE);
        }
        REQUIRE(neighborsCount > 1 && neighborsCount < 256);
        free(b); errno = 1976;
        REQUIRE(realloc(a, 2048) == nullptr && errno == ENOMEM && Pattern(a, 1024, 0xAD));
        b = malloc(128);
        REQUIRE(b && (unsigned)b == holeAddress && Pattern(a, 1024, 0xAD));
        for (unsigned i = 0; i < neighborsCount; ++i) REQUIRE(Pattern(neighbors[i], 256, i ^ 0xCE));
        free(a); free(b);
        for (unsigned i = 0; i < neighborsCount; ++i) free(neighbors[i]);
        Empty();
        a = malloc(1024); b = calloc(1024, 1);
        REQUIRE(a && b && Zero(b, 1024)); Fill(a, 1024, 0x51); Fill(b, 1024, 0x62);
        void* moved = realloc(a, 4096);
        REQUIRE(moved && moved != a && Pattern(moved, 1024, 0x51) && Pattern(b, 1024, 0x62));
        Fill(moved, 4096, 0x73); errno = 1976;
        REQUIRE(realloc(moved, huge) == nullptr && errno == ENOMEM
            && Pattern(moved, 4096, 0x73) && Pattern(b, 1024, 0x62));
        REQUIRE(realloc(moved, 17) == moved && Pattern(moved, 17, 0x73) && Pattern(b, 1024, 0x62));
        free(b); REQUIRE(realloc(moved, 4096) == moved && Pattern(moved, 17, 0x73));
        REQUIRE(realloc(moved, 0) == nullptr); Empty();
        void* blocks[20]; unsigned count = 0;
        for (; count < 20; ++count) {
            blocks[count] = malloc(4096);
            if (!blocks[count]) break;
            Fill(blocks[count], 4096, count ^ 0xB1);
        }
        REQUIRE(count > 1 && count < 20 && errno == ENOMEM);
        const GtosVmRegionInfo fragmented = Query();
        REQUIRE(realloc(blocks[0], 8192) == nullptr && errno == ENOMEM);
        for (unsigned i = 0; i < count; ++i) REQUIRE(Pattern(blocks[i], 4096, i ^ 0xB1));
        const GtosVmRegionInfo unchanged = Query();
        REQUIRE(unchanged.handle == fragmented.handle && unchanged.base == fragmented.base);
        for (unsigned i = 0; i < count; ++i) free(blocks[i]);
        Empty();
        const size_t alignments[] = {4, 8, 16, 32, 64, 256, 4096, 32768};
        for (unsigned i = 0; i < sizeof(alignments) / sizeof(alignments[0]); ++i) {
            output = nullptr; errno = 1976;
            REQUIRE(posix_memalign(&output, alignments[i], 37) == 0 && output
                && !((unsigned)output & (alignments[i] - 1U)) && errno == 1976);
            Fill(output, 37, 0xE4);
            moved = realloc(output, 4096);
            REQUIRE(moved && !((unsigned)moved & (alignments[i] - 1U)) && Pattern(moved, 37, 0xE4));
            free(moved); Empty();
        }
        output = (void*)0x12345670U; errno = 1976;
        REQUIRE(posix_memalign(&output, 65536, 1) == ENOMEM && output == (void*)0x12345670U && errno == 1976); Empty();
        for (unsigned round = 0; round < 64; ++round) {
            const unsigned length = 1 + round * 3;
            a = malloc(length); b = malloc(129);
            REQUIRE(a && b); Fill(a, length, round); Fill(b, 129, round ^ 0xD2);
            moved = realloc(a, length + 33);
            REQUIRE(moved && Pattern(moved, length, round) && Pattern(b, 129, round ^ 0xD2));
            free(moved); free(b); Empty();
        }
    }
#endif
    void Publish(void* primary, unsigned primaryBytes, void* neighbor, unsigned neighborBytes) {
        const GtosVmRegionInfo info = Query();
        native_heap_record.base = info.base; native_heap_record.handle = info.handle;
        native_heap_record.primary = (unsigned)primary; native_heap_record.primaryBytes = primaryBytes;
        native_heap_record.neighbor = (unsigned)neighbor; native_heap_record.neighborBytes = neighborBytes;
    }
}
extern "C" void NativeEntry() {
#if GTOS_HEAP_PROBE_MODE == 0
    Positive();
#endif
    void* primary = malloc(4096); void* neighbor = malloc(4096);
    REQUIRE(primary && neighbor && primary != neighbor
        && !((unsigned)primary & 15U) && !((unsigned)neighbor & 15U));
    Fill(primary, 4096, 0x37); Fill(neighbor, 4096, 0xA9);
    Publish(primary, 4096, neighbor, 4096);
    native_heap_record.stage = 1;
#if GTOS_HEAP_PROBE_MODE == 1
    *(volatile unsigned*)0xBFFFCFFCU = 0xF00D;
    Fail(__LINE__);
#elif GTOS_HEAP_PROBE_MODE == 2
    for (;;) Call(GTOS_SYS_YIELD);
#elif GTOS_HEAP_PROBE_MODE == 3 || GTOS_HEAP_PROBE_MODE == 4
    const unsigned oldHandle = native_heap_record.handle;
    free(primary); free(neighbor); Empty();
    native_heap_record.primary = 0; native_heap_record.primaryBytes = 0;
    native_heap_record.neighbor = 0; native_heap_record.neighborBytes = 0;
#if GTOS_HEAP_PROBE_MODE == 4
    primary = malloc(4096);
    REQUIRE(primary && Zero(primary, 4096));
    Publish(primary, 4096, nullptr, 0);
    REQUIRE(native_heap_record.handle > oldHandle);
#else
    REQUIRE(oldHandle != 0);
#endif
    native_heap_record.stage = 2; Exit(0);
#elif GTOS_HEAP_PROBE_MODE == 5
    free((unsigned char*)primary + 16); Fail(__LINE__);
#elif GTOS_HEAP_PROBE_MODE == 6
    free(neighbor);
    native_heap_record.neighbor = 0; native_heap_record.neighborBytes = 0;
    free(neighbor); Fail(__LINE__);
#elif GTOS_HEAP_PROBE_MODE == 7
    // The C/POSIX caller must provide a writable pointer object. A library
    // cannot provide the kernel's checked-copy guarantee for this RO output.
    posix_memalign((void**)0x40000000U, 16, 4096); Fail(__LINE__);
#else
    REQUIRE(Pattern(primary, 4096, 0x37) && Pattern(neighbor, 4096, 0xA9));
    native_heap_record.stage = 2; Exit(0);
#endif
}
