#include <stdint.h>
#include <stdlib.h>
#include <process/abi.h>
#include "absl/base/internal/low_level_alloc.h"
#include "absl/base/internal/gtos-low-level-pages.h"
#include "heap.h"
#include "record.h"

using absl::base_internal::LowLevelAlloc;
using absl::base_internal::GtosLowLevelMapPages;
using absl::base_internal::GtosLowLevelUnmapPages;
extern "C" volatile unsigned native_v8_integration_stage;
extern "C" {
volatile NativeDiagRecord native_diag_record
    __attribute__((section(".data.diag_record"), used)) = {1,0,0,0,0,0,0,0,0,0,0,0,{0,0,0,0}};
}
namespace {
int Call(unsigned number, const void* request, unsigned bytes) {
    asm volatile("int $0x80" : "+a"(number)
        : "b"((unsigned)request), "c"(bytes) : "memory", "cc");
    return (int)number;
}
void Check(bool value, unsigned error) {
    native_diag_record.checks = native_diag_record.checks + 1;
    if (!value) {
        native_diag_record.error = error;
        Call(GTOS_SYS_EXIT, (const void*)99U, 0);
        __builtin_trap();
    }
}
void Done(unsigned bit) { native_diag_record.textBytes = native_diag_record.textBytes | (1U << bit); }
GtosVmReserveResult Reserve(unsigned length, unsigned hint = 0) {
    GtosVmReserveResult result = {};
    GtosVmReserveRequest request = {1, length, 4096, hint, (unsigned)&result};
    if (Call(GTOS_SYS_VM_RESERVE, &request, sizeof(request)) != 0) result.handle = 0;
    return result;
}
void Release(unsigned handle) {
    GtosVmControlRequest request = {1, handle, 0};
    Check(Call(GTOS_SYS_VM_RELEASE, &request, sizeof(request)) == 0, 101);
}
void Fill(void* p, unsigned size, unsigned char value) {
    for (unsigned i = 0; i < size; ++i) ((volatile unsigned char*)p)[i] = value;
}
bool Pattern(void* p, unsigned size, unsigned char value) {
    for (unsigned i = 0; i < size; ++i) if (((volatile unsigned char*)p)[i] != value) return false;
    return true;
}
unsigned Slots(bool exercise) {
    unsigned handles[32] = {}, count = 0;
    for (; count < 32; ++count) {
        GtosVmReserveResult r = Reserve(4096);
        if (!r.handle) break;
        handles[count] = r.handle;
    }
    Check(count > 0 && count < 32 && GtosLowLevelMapPages(4096, 4096) == nullptr, 102);
    if (exercise) {
        Release(handles[--count]);
        void* recovered = GtosLowLevelMapPages(4096, 4096);
        Check(recovered != nullptr && GtosLowLevelUnmapPages(recovered, 4096), 103);
    }
    const unsigned capacity = count + (exercise ? 1U : 0U);
    while (count) Release(handles[--count]);
    return capacity;
}
struct BeforeMain {
    BeforeMain() {
        GtosVmRegionInfo heap = {};
        Check(native_v8_integration_stage == 1 && gtos_native_heap_query(&heap) == 0, 1);
        GtosVmReserveResult marker = Reserve(4096, 0x80000000U);
        Check(marker.handle && marker.base == 0x80000000U, 2);
        GtosVmRangeRequest commit = {1, marker.handle, 0, 4096, GTOS_VM_READ_WRITE};
        Check(Call(GTOS_SYS_VM_SET_PERMISSIONS, &commit, sizeof(commit)) == 0, 3);
        *(volatile unsigned*)marker.base = 0xC011CA11U;
        native_diag_record.region = marker.handle;
        native_diag_record.base = marker.base;
        native_diag_record.length = marker.length;
        Check(LowLevelAlloc::Alloc(0) == nullptr, 4);
        Check(gtos_native_heap_query(&heap) == 1 && heap.length == 65536 && heap.resident_pages == 16, 5);
        native_diag_record.reserved[0] = heap.base;
        native_diag_record.stage = 1;
        Done(0);
    }
} before_main;
}

extern "C" int NativeV8IsolateMain() {
    Check(native_v8_integration_stage == 2 && native_diag_record.stage == 1 && native_diag_record.textBytes == 1, 10);
    LowLevelAlloc::Free(nullptr);
    Check(LowLevelAlloc::DefaultArena() == LowLevelAlloc::DefaultArena(), 11);
    void* a = LowLevelAlloc::Alloc(1);
    void* b = LowLevelAlloc::Alloc(37);
    void* c = LowLevelAlloc::Alloc(60000);
    Fill(a, 1, 0xA1); Fill(b, 37, 0xB2); Fill(c, 60000, 0xC3);
    LowLevelAlloc::Free(a);
    void* reuse = LowLevelAlloc::Alloc(1);
    Check(reuse == a && Pattern(b, 37, 0xB2) && Pattern(c, 60000, 0xC3), 12);
    LowLevelAlloc::Free(reuse); LowLevelAlloc::Free(b); LowLevelAlloc::Free(c);
    Done(1);

    auto* arena = LowLevelAlloc::NewArena(0);
    a = LowLevelAlloc::AllocWithArena(60000, arena);
    b = LowLevelAlloc::AllocWithArena(60000, arena);
    Fill(a, 60000, 0x11); Fill(b, 60000, 0x22);
    // IA32 headers are 16 bytes. The first chunk's remaining 5520 bytes
    // coalesce with the next 64 KiB reservation before the second split.
    Check((uintptr_t)b == (uintptr_t)a + 60016U, 20);
    Check(!LowLevelAlloc::DeleteArena(arena) && Pattern(a, 60000, 0x11) && Pattern(b, 60000, 0x22), 21);
    const unsigned pages = (unsigned)a & ~4095U;
    LowLevelAlloc::Free(a); LowLevelAlloc::Free(b);
    Check(LowLevelAlloc::DeleteArena(arena), 22);
    GtosVmReserveResult reclaimed = Reserve(131072, pages);
    Check(reclaimed.handle && reclaimed.base == pages, 23);
    Release(reclaimed.handle);
    arena = LowLevelAlloc::NewArena(LowLevelAlloc::kCallMallocHook);
    Check(LowLevelAlloc::DeleteArena(arena), 24);
    Done(2);

    void* large = malloc(64000);
    Check(large != nullptr, 30);
    void* held[64] = {};
    unsigned count = 0;
    while (count < 64) {
        void* p = malloc(1);
        if (!p) break;
        held[count++] = p;
    }
    Check(count < 64 && malloc(1) == nullptr, 31);
    arena = LowLevelAlloc::NewArena(0);
    a = LowLevelAlloc::AllocWithArena(60000, arena);
    Fill(a, 60000, 0x44);
    Check(Pattern(a, 60000, 0x44), 32);
    LowLevelAlloc::Free(a);
    Check(LowLevelAlloc::DeleteArena(arena), 33);
    while (count) free(held[--count]);
    free(large);
    Done(3);

    Check(!GtosLowLevelMapPages(0, 4096) && !GtosLowLevelMapPages(4097, 4096)
        && !GtosLowLevelMapPages(4096, 3) && !GtosLowLevelUnmapPages((void*)0xFFFFF000U, 8192), 40);
    a = GtosLowLevelMapPages(8192, 4096);
    Check(a && Pattern(a, 8192, 0), 41);
    Fill(a, 8192, 0x55);
    GtosVmReserveResult gap = Reserve(4096);
    b = GtosLowLevelMapPages(4096, 4096);
    Check(gap.handle && gap.base == (unsigned)a + 8192 && (unsigned)b == gap.base + 4096, 42);
    Check(!GtosLowLevelUnmapPages((char*)a + 4096, 4096) && !GtosLowLevelUnmapPages(a, 4096)
        && !GtosLowLevelUnmapPages(a, 16384) && Pattern(a, 8192, 0x55), 43);
    Check(GtosLowLevelUnmapPages(a, 8192) && GtosLowLevelUnmapPages(b, 4096)
        && !GtosLowLevelUnmapPages(a, 8192), 44);
    Release(gap.handle);
    Done(4);

    const unsigned slots = Slots(true);
    Done(5);
    GtosVmReserveResult scratch = Reserve(256 * 4096);
    Check(scratch.handle != 0, 50);
    unsigned committed = 0;
    int failure = 0;
    for (; committed < 256; ++committed) {
        GtosVmRangeRequest range = {1, scratch.handle, committed * 4096, 4096, GTOS_VM_READ_WRITE};
        failure = Call(GTOS_SYS_VM_SET_PERMISSIONS, &range, sizeof(range));
        if (failure != 0) break;
    }
    Check(committed > 0 && committed < 256 && (failure == GTOS_VM_ERR_LIMIT || failure == GTOS_VM_ERR_NO_MEMORY), 51);
    bool rolled_back = true;
    for (unsigned i = 0; i < 40; ++i) if (GtosLowLevelMapPages(4096, 4096)) rolled_back = false;
    Check(rolled_back, 52);
    GtosVmRangeRequest decommit = {1, scratch.handle, 0, 4096, 0};
    Check(Call(GTOS_SYS_VM_DECOMMIT, &decommit, sizeof(decommit)) == 0, 53);
    a = GtosLowLevelMapPages(4096, 4096);
    Check(a && GtosLowLevelUnmapPages(a, 4096), 54);
    Release(scratch.handle);
    Check(Slots(false) == slots, 55);
    Done(6);
    GtosVmRegionInfo heap = {};
    Check(gtos_native_heap_query(&heap) == 1 && heap.base == native_diag_record.reserved[0]
        && heap.resident_pages == 16 && *(volatile unsigned*)native_diag_record.base == 0xC011CA11U, 60);
    Done(7);
    native_diag_record.stage = 2;
    if (native_diag_record.mode == 1) {
        LowLevelAlloc::Alloc(0xFFFFFFF0U);
    } else if (native_diag_record.mode == 2) {
        a = LowLevelAlloc::Alloc(37);
        LowLevelAlloc::Free(a);
        LowLevelAlloc::Free(a);
    } else if (native_diag_record.mode == 3) {
        arena = LowLevelAlloc::NewArena(0);
        // 256 pages fit the per-request limit, but exceed the remaining
        // resident-page quota after the 49 live pages established above.
        LowLevelAlloc::AllocWithArena(256U * 4096U - 16U, arena);
    } else if (native_diag_record.mode == 4) {
        arena = LowLevelAlloc::NewArena(0);
        a = LowLevelAlloc::AllocWithArena(60000, arena);
        b = LowLevelAlloc::AllocWithArena(60000, arena);
        Fill(a, 60000, 0x66); Fill(b, 60000, 0x77);
        native_diag_record.formattedBytes = (unsigned)a;
        native_diag_record.expectedBytes = (unsigned)b;
        native_diag_record.stage = 3;
        for (;;) {
            // Cancellation deliberately retains two live allocator chunks.
            if (!Pattern(a, 60000, 0x66) || !Pattern(b, 60000, 0x77)) Check(false, 61);
            native_diag_record.progress = native_diag_record.progress + 1;
            Call(GTOS_SYS_YIELD, nullptr, 0);
        }
    }
    if (native_diag_record.mode != 0) Check(false, 62);
    return 0;
}
