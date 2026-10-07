// Real CPL3 consumer of the production VM wire ABI; no kernel policy substitute.
#include "record.h"
#include <process/abi.h>
#include <process/vm_abi.h>
#ifndef GTOS_VM_PROBE_MODE
#define GTOS_VM_PROBE_MODE 0
#endif
#if GTOS_VM_PROBE_MODE < 0 || GTOS_VM_PROBE_MODE > 6
#error Unknown VM probe mode
#endif
extern "C" {
    volatile VmProbeRecord native_vm_record __attribute__((section(".data.vm_record"), used, aligned(4)))
        = {1, GTOS_VM_PROBE_MODE, 0, 0, 0, 0};
}
namespace {
    const uint32_t Length = 16 * 1024 * 1024, Page = 4096, Hint = 0x81000000U;
    inline uint32_t Call(uint32_t operation, uint32_t first = 0, uint32_t second = 0) {
        uint32_t result;
        asm volatile("int $0x80" : "=a"(result)
            : "a"(operation), "b"(first), "c"(second) : "memory", "cc");
        return result;
    }
    inline int32_t Wire(uint32_t operation, const void* request, uint32_t bytes) {
        return (int32_t)Call(operation, (uint32_t)request, bytes);
    }
    __attribute__((noreturn)) void Exit(uint32_t code) {
        Call(GTOS_SYS_EXIT, code);
        for (;;) asm volatile("ud2");
    }
    __attribute__((noreturn)) void Fail(uint32_t line) {
        native_vm_record.stage = 0x80000000U | line;
        const char text[] = "GTOS VM PROBE FAIL V1\n";
        Call(GTOS_SYS_WRITE, (uint32_t)text, sizeof(text) - 1);
        Exit(line);
    }
#define REQUIRE(value) do { if (!(value)) Fail(__LINE__); } while (0)
    inline void Fill(uint32_t address, uint32_t bytes, uint8_t value) {
        volatile uint8_t* output = (volatile uint8_t*)address;
        for (uint32_t i = 0; i < bytes; ++i) output[i] = value;
    }
    inline bool All(uint32_t address, uint32_t bytes, uint8_t value) {
        const volatile uint8_t* input = (const volatile uint8_t*)address;
        for (uint32_t i = 0; i < bytes; ++i) if (input[i] != value) return false;
        return true;
    }
    inline int32_t Range(uint32_t operation, uint32_t handle, uint32_t offset,
                         uint32_t bytes, uint32_t protection = 0) {
        const GtosVmRangeRequest request = {GTOS_VM_ABI_VERSION, handle, offset, bytes, protection};
        return Wire(operation, &request, sizeof(request));
    }
    inline int32_t Control(uint32_t operation, uint32_t handle, uint32_t bytes = 0) {
        const GtosVmControlRequest request = {GTOS_VM_ABI_VERSION, handle, bytes};
        return Wire(operation, &request, sizeof(request));
    }
#if GTOS_VM_PROBE_MODE == 0
    inline GtosVmRegionInfo Query(uint32_t handle) {
        GtosVmRegionInfo info = {};
        const GtosVmQueryRequest request = {GTOS_VM_ABI_VERSION, handle, (uint32_t)&info};
        REQUIRE(Wire(GTOS_SYS_VM_QUERY, &request, sizeof(request)) == 0);
        REQUIRE(info.version == GTOS_VM_ABI_VERSION && info.handle == handle);
        return info;
    }
    inline GtosVmReserveResult Reserve(uint32_t bytes, uint32_t hint = 0) {
        GtosVmReserveResult result = {};
        const GtosVmReserveRequest request = {GTOS_VM_ABI_VERSION, bytes, Page, hint, (uint32_t)&result};
        REQUIRE(Wire(GTOS_SYS_VM_RESERVE, &request, sizeof(request)) == 0);
        REQUIRE(result.version == GTOS_VM_ABI_VERSION && result.handle
            && result.handle <= 0x7FFFFFFFU && result.base >= 0x80000000U
            && result.base < 0xBFFFC000U && result.length == bytes && result.page_size == Page);
        return result;
    }
    inline uint32_t Word(uint32_t address) {
        const volatile uint8_t* bytes = (const volatile uint8_t*)address;
        return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8)
            | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    }
    void InvalidHandle(uint32_t handle) {
        REQUIRE(Range(GTOS_SYS_VM_SET_PERMISSIONS, handle, 0, Page, GTOS_VM_READ_WRITE) == GTOS_VM_ERR_BAD_STATE);
        REQUIRE(Range(GTOS_SYS_VM_DECOMMIT, handle, 0, Page) == GTOS_VM_ERR_BAD_STATE);
        REQUIRE(Range(GTOS_SYS_VM_DISCARD, handle, 0, Page) == GTOS_VM_ERR_BAD_STATE);
        REQUIRE(Control(GTOS_SYS_VM_RELEASE, handle) == GTOS_VM_ERR_BAD_STATE);
        REQUIRE(Control(GTOS_SYS_VM_TRIM, handle, Page) == GTOS_VM_ERR_BAD_STATE);
        GtosVmRegionInfo output;
        Fill((uint32_t)&output, sizeof(output), 0xA5);
        const GtosVmQueryRequest request = {GTOS_VM_ABI_VERSION, handle, (uint32_t)&output};
        REQUIRE(Wire(GTOS_SYS_VM_QUERY, &request, sizeof(request)) == GTOS_VM_ERR_BAD_STATE);
        REQUIRE(All((uint32_t)&output, sizeof(output), 0xA5));
    }
    void Prechecks(uint32_t base, uint32_t handle) {
        const struct { uint32_t operation, bytes; } calls[] = {
            {GTOS_SYS_VM_RESERVE, GTOS_VM_RESERVE_REQUEST_BYTES},
            {GTOS_SYS_VM_SET_PERMISSIONS, GTOS_VM_RANGE_REQUEST_BYTES},
            {GTOS_SYS_VM_DECOMMIT, GTOS_VM_RANGE_REQUEST_BYTES},
            {GTOS_SYS_VM_DISCARD, GTOS_VM_RANGE_REQUEST_BYTES},
            {GTOS_SYS_VM_RELEASE, GTOS_VM_CONTROL_REQUEST_BYTES},
            {GTOS_SYS_VM_TRIM, GTOS_VM_CONTROL_REQUEST_BYTES},
            {GTOS_SYS_VM_QUERY, GTOS_VM_QUERY_REQUEST_BYTES}
        };
        for (uint32_t i = 0; i < sizeof(calls) / sizeof(calls[0]); ++i) {
            REQUIRE(Wire(calls[i].operation, (const void*)0, calls[i].bytes - 1) == GTOS_VM_ERR_BAD_SIZE);
            REQUIRE(Wire(calls[i].operation, (const void*)0, calls[i].bytes) == GTOS_VM_ERR_BAD_ADDRESS);
            REQUIRE(Wire(calls[i].operation, (const void*)(base + 3 * Page - 4), calls[i].bytes)
                == GTOS_VM_ERR_BAD_ADDRESS); // Full request crosses into an unmapped page.
        }
        const GtosVmReserveRequest wrongReserve = {2, 0, 0, 0, 0};
        REQUIRE(Wire(GTOS_SYS_VM_RESERVE, &wrongReserve, sizeof(wrongReserve)) == GTOS_VM_ERR_UNSUPPORTED_VERSION);
        const GtosVmRangeRequest wrongRange = {2, handle, 0, Page, 0};
        REQUIRE(Wire(GTOS_SYS_VM_SET_PERMISSIONS, &wrongRange, sizeof(wrongRange)) == GTOS_VM_ERR_UNSUPPORTED_VERSION);
        REQUIRE(Wire(GTOS_SYS_VM_DECOMMIT, &wrongRange, sizeof(wrongRange)) == GTOS_VM_ERR_UNSUPPORTED_VERSION);
        REQUIRE(Wire(GTOS_SYS_VM_DISCARD, &wrongRange, sizeof(wrongRange)) == GTOS_VM_ERR_UNSUPPORTED_VERSION);
        const GtosVmControlRequest wrongControl = {2, handle, Page};
        REQUIRE(Wire(GTOS_SYS_VM_RELEASE, &wrongControl, sizeof(wrongControl)) == GTOS_VM_ERR_UNSUPPORTED_VERSION);
        REQUIRE(Wire(GTOS_SYS_VM_TRIM, &wrongControl, sizeof(wrongControl)) == GTOS_VM_ERR_UNSUPPORTED_VERSION);
        const GtosVmQueryRequest wrongQuery = {2, handle, 0};
        REQUIRE(Wire(GTOS_SYS_VM_QUERY, &wrongQuery, sizeof(wrongQuery)) == GTOS_VM_ERR_UNSUPPORTED_VERSION);
        // All output failures occur in healthy checked-copy prevalidation. They
        // must not consume a handle or write even the mapped output prefix.
        const uint32_t outputs[] = {base + 3 * Page - 8, 0xFFFFFFFCU, 0};
        for (uint32_t i = 0; i < sizeof(outputs) / sizeof(outputs[0]); ++i) {
            const GtosVmReserveRequest reserve = {GTOS_VM_ABI_VERSION, Page, Page, 0, outputs[i]};
            REQUIRE(Wire(GTOS_SYS_VM_RESERVE, &reserve, sizeof(reserve)) == GTOS_VM_ERR_BAD_ADDRESS);
            const GtosVmQueryRequest query = {GTOS_VM_ABI_VERSION, handle, outputs[i]};
            REQUIRE(Wire(GTOS_SYS_VM_QUERY, &query, sizeof(query)) == GTOS_VM_ERR_BAD_ADDRESS);
            REQUIRE(All(base + 3 * Page - 8, 8, 0x6B));
        }
        REQUIRE(Range(GTOS_SYS_VM_SET_PERMISSIONS, handle, 2 * Page, Page, GTOS_VM_READ) == 0);
        const uint32_t readonlyOutputs[] = {base + 2 * Page + 32, base + 2 * Page - 8};
        for (uint32_t i = 0; i < sizeof(readonlyOutputs) / sizeof(readonlyOutputs[0]); ++i) {
            const GtosVmReserveRequest reserve = {GTOS_VM_ABI_VERSION, Page, Page, 0, readonlyOutputs[i]};
            REQUIRE(Wire(GTOS_SYS_VM_RESERVE, &reserve, sizeof(reserve)) == GTOS_VM_ERR_BAD_ADDRESS);
            const GtosVmQueryRequest query = {GTOS_VM_ABI_VERSION, handle, readonlyOutputs[i]};
            REQUIRE(Wire(GTOS_SYS_VM_QUERY, &query, sizeof(query)) == GTOS_VM_ERR_BAD_ADDRESS);
            if (!i) REQUIRE(All(readonlyOutputs[i], 20, 0x6B));
            else REQUIRE(All(readonlyOutputs[i], 8, 0x5A) && All(base + 2 * Page, 12, 0x6B));
        }
        REQUIRE(Range(GTOS_SYS_VM_SET_PERMISSIONS, handle, 2 * Page, Page, GTOS_VM_READ_WRITE) == 0);
        GtosVmReserveResult output;
        Fill((uint32_t)&output, sizeof(output), 0xA5);
        const GtosVmReserveRequest badRange = {GTOS_VM_ABI_VERSION, 0, Page, 0, (uint32_t)&output};
        REQUIRE(Wire(GTOS_SYS_VM_RESERVE, &badRange, sizeof(badRange)) == GTOS_VM_ERR_RANGE);
        REQUIRE(All((uint32_t)&output, sizeof(output), 0xA5));
        const GtosVmReserveRequest conflict = {GTOS_VM_ABI_VERSION, Page, Page, base, (uint32_t)&output};
        REQUIRE(Wire(GTOS_SYS_VM_RESERVE, &conflict, sizeof(conflict)) == GTOS_VM_ERR_CONFLICT);
        REQUIRE(All((uint32_t)&output, sizeof(output), 0xA5));
        const GtosVmReserveRequest badAlignment = {GTOS_VM_ABI_VERSION, Page, 8193, 0, (uint32_t)&output};
        REQUIRE(Wire(GTOS_SYS_VM_RESERVE, &badAlignment, sizeof(badAlignment)) == GTOS_VM_ERR_RANGE);
        REQUIRE(All((uint32_t)&output, sizeof(output), 0xA5));
        const GtosVmReserveResult healthy = Reserve(4 * Page);
        // The independently running peer holds its one reservation and only
        // yields. No other producer reserves during this bounded positive case.
        REQUIRE(healthy.handle == handle + 1);
        REQUIRE(Control(GTOS_SYS_VM_TRIM, healthy.handle, 2 * Page) == 0);
        REQUIRE(Control(GTOS_SYS_VM_TRIM, healthy.handle, 2 * Page) == 0);
        const GtosVmRegionInfo trimmed = Query(healthy.handle);
        REQUIRE(trimmed.base == healthy.base && trimmed.length == 2 * Page && !trimmed.resident_pages);
        REQUIRE(Control(GTOS_SYS_VM_TRIM, healthy.handle, 3 * Page) == GTOS_VM_ERR_RANGE);
        REQUIRE(Control(GTOS_SYS_VM_TRIM, healthy.handle, 0) == GTOS_VM_ERR_RANGE);
        REQUIRE(Control(GTOS_SYS_VM_RELEASE, healthy.handle) == 0);
        InvalidHandle(healthy.handle);
    }
    void OverlapAndHighOutput(uint32_t base, uint32_t handle) {
        // One unsigned-word object avoids incompatible struct type punning.
        // The request is completely overwritten by its own result only after
        // the kernel has snapshotted all five request words.
        uint32_t words[5] = {GTOS_VM_ABI_VERSION, Page, Page, 0, (uint32_t)words};
        REQUIRE(Wire(GTOS_SYS_VM_RESERVE, words, GTOS_VM_RESERVE_REQUEST_BYTES) == 0);
        REQUIRE(words[0] == GTOS_VM_ABI_VERSION && words[1] && words[2] >= 0x80000000U
            && words[3] == Page && words[4] == Page);
        const uint32_t oldHandle = words[1], oldBase = words[2];
        REQUIRE(Control(GTOS_SYS_VM_RELEASE, oldHandle) == 0);
        const GtosVmReserveResult replacement = Reserve(Page, oldBase);
        REQUIRE(replacement.base == oldBase && replacement.handle > oldHandle);
        InvalidHandle(oldHandle);
        REQUIRE(Control(GTOS_SYS_VM_RELEASE, replacement.handle) == 0);
        words[0] = GTOS_VM_ABI_VERSION; words[1] = handle; words[2] = (uint32_t)words;
        words[3] = words[4] = 0xA5A5A5A5U;
        REQUIRE(Wire(GTOS_SYS_VM_QUERY, words, GTOS_VM_QUERY_REQUEST_BYTES) == 0);
        REQUIRE(words[0] == GTOS_VM_ABI_VERSION && words[1] == handle && words[2] == base
            && words[3] == Length && words[4] == 3);
        // Both result addresses carry bit 31. Success is conveyed in EAX=0;
        // base addresses themselves are checked-copied unsigned wire data.
        const uint32_t highOutput = base + 2 * Page + 512;
        const GtosVmReserveRequest request = {GTOS_VM_ABI_VERSION, Page, Page, 0, highOutput};
        REQUIRE(Wire(GTOS_SYS_VM_RESERVE, &request, sizeof(request)) == 0);
        REQUIRE(Word(highOutput) == GTOS_VM_ABI_VERSION && Word(highOutput + 4)
            && Word(highOutput + 8) >= 0x80000000U && Word(highOutput + 12) == Page
            && Word(highOutput + 16) == Page);
        const uint32_t highHandle = Word(highOutput + 4);
        const GtosVmQueryRequest query = {GTOS_VM_ABI_VERSION, handle, highOutput + 256};
        REQUIRE(Wire(GTOS_SYS_VM_QUERY, &query, sizeof(query)) == 0);
        REQUIRE(Word(highOutput + 256) == GTOS_VM_ABI_VERSION && Word(highOutput + 260) == handle
            && Word(highOutput + 264) == base && Word(highOutput + 268) == Length
            && Word(highOutput + 272) == 3);
        REQUIRE(Control(GTOS_SYS_VM_RELEASE, highHandle) == 0);
        Fill(base + 2 * Page, Page, 0x6B);
    }
    void Adversarial(uint32_t base, uint32_t handle) {
        Prechecks(base, handle);
        OverlapAndHighOutput(base, handle);
        REQUIRE(native_vm_record.foreign_handle && native_vm_record.foreign_handle != handle);
        InvalidHandle(native_vm_record.foreign_handle);
        InvalidHandle(0); InvalidHandle(0xFFFFFFFFU);
        const uint32_t denied[] = {2, 4, 5, 7, 0xFFFFFFFFU};
        for (uint32_t i = 0; i < sizeof(denied) / sizeof(denied[0]); ++i)
            REQUIRE(Range(GTOS_SYS_VM_SET_PERMISSIONS, handle, 0, Page, denied[i]) == GTOS_VM_ERR_PERMISSION);
        REQUIRE(Range(GTOS_SYS_VM_SET_PERMISSIONS, handle, Length - Page, 2 * Page, GTOS_VM_READ_WRITE)
            == GTOS_VM_ERR_RANGE);
        REQUIRE(Range(GTOS_SYS_VM_DECOMMIT, handle, 0, Page, GTOS_VM_READ) == GTOS_VM_ERR_BAD_STATE);
        REQUIRE(Range(GTOS_SYS_VM_DISCARD, handle, 0, Page, GTOS_VM_READ) == GTOS_VM_ERR_BAD_STATE);
        REQUIRE(Control(GTOS_SYS_VM_RELEASE, handle, Page) == GTOS_VM_ERR_BAD_STATE);
        REQUIRE(Range(GTOS_SYS_VM_DISCARD, handle, 0, 4 * Page) == GTOS_VM_ERR_BAD_STATE);
        REQUIRE(All(base, Page, 0) && All(base + Page, Page, 0x5A) && All(base + 2 * Page, Page, 0x6B));
        REQUIRE(Range(GTOS_SYS_VM_SET_PERMISSIONS, handle, 3 * Page, Page, GTOS_VM_NONE) == 0);
        REQUIRE(Query(handle).resident_pages == 3); // NONE on a hole allocates nothing.
        REQUIRE(Range(GTOS_SYS_VM_SET_PERMISSIONS, handle, 2 * Page, Page, GTOS_VM_NONE) == 0);
        REQUIRE(Query(handle).resident_pages == 3); // NONE retains committed ownership.
        REQUIRE(Range(GTOS_SYS_VM_DISCARD, handle, 2 * Page, Page) == 0);
        REQUIRE(Range(GTOS_SYS_VM_SET_PERMISSIONS, handle, 2 * Page, Page, GTOS_VM_READ) == 0);
        REQUIRE(All(base + 2 * Page, Page, 0));
        REQUIRE(Range(GTOS_SYS_VM_SET_PERMISSIONS, handle, 2 * Page, Page, GTOS_VM_READ_WRITE) == 0);
        Fill(base + 2 * Page, Page, 0x6B);
        REQUIRE(Range(GTOS_SYS_VM_DECOMMIT, handle, 2 * Page, Page) == 0);
        REQUIRE(Range(GTOS_SYS_VM_DECOMMIT, handle, 2 * Page, Page) == 0);
        REQUIRE(Query(handle).resident_pages == 2);
        REQUIRE(Range(GTOS_SYS_VM_SET_PERMISSIONS, handle, 2 * Page, Page, GTOS_VM_READ_WRITE) == 0);
        REQUIRE(All(base + 2 * Page, Page, 0));
        Fill(base + 2 * Page, Page, 0x6B);
        const GtosVmRegionInfo info = Query(handle);
        REQUIRE(info.base == base && info.length == Length && info.resident_pages == 3);
        REQUIRE(All(base, Page, 0) && All(base + Page, Page, 0x5A) && All(base + 2 * Page, Page, 0x6B));
    }
#endif
}
extern "C" __attribute__((noreturn)) void NativeEntry() {
    REQUIRE(Call(GTOS_SYS_ABI) == GTOS_NATIVE_ABI_VERSION);
    GtosVmReserveResult result = {};
    const GtosVmReserveRequest reserve = {GTOS_VM_ABI_VERSION, Length, Page, Hint, (uint32_t)&result};
    REQUIRE(Wire(GTOS_SYS_VM_RESERVE, &reserve, sizeof(reserve)) == 0);
    REQUIRE(result.version == GTOS_VM_ABI_VERSION && result.base == Hint && result.handle
        && result.handle <= 0x7FFFFFFFU && result.length == Length && result.page_size == Page);
    const uint32_t base = result.base, handle = result.handle;
    native_vm_record.base = base; native_vm_record.handle = handle;
    REQUIRE(Range(GTOS_SYS_VM_SET_PERMISSIONS, handle, 0, 3 * Page, GTOS_VM_READ_WRITE) == 0);
    REQUIRE(All(base, 3 * Page, 0));
    // Actual volatile reads and writes populate the RW TLB before transitions.
    Fill(base, Page, 0x27); Fill(base + Page, Page, 0x5A); Fill(base + 2 * Page, Page, 0x6B);
    REQUIRE(All(base, Page, 0x27) && All(base + Page, Page, 0x5A) && All(base + 2 * Page, Page, 0x6B));
    Fill(base, Page, 0); REQUIRE(All(base, Page, 0));
    native_vm_record.stage = 1;
#if GTOS_VM_PROBE_MODE == 0
    Adversarial(base, handle);
    native_vm_record.stage = 2;
    const uint32_t start = Call(GTOS_SYS_TICKS);
    while (Call(GTOS_SYS_TICKS) - start < 20) Call(GTOS_SYS_YIELD);
    Exit(0); // Three committed frames remain for stopped-before-Reap inspection.
#elif GTOS_VM_PROBE_MODE == 1
    REQUIRE(Range(GTOS_SYS_VM_SET_PERMISSIONS, handle, 0, Page, GTOS_VM_READ) == 0);
    *(volatile uint8_t*)base = 0xFF; // Expected user protection write PF=7.
    Fail(__LINE__);
#elif GTOS_VM_PROBE_MODE == 2
    REQUIRE(Range(GTOS_SYS_VM_SET_PERMISSIONS, handle, 0, Page, GTOS_VM_NONE) == 0);
    const uint8_t value = *(volatile uint8_t*)base; (void)value; // Expected user nonpresent read PF=4.
    Fail(__LINE__);
#elif GTOS_VM_PROBE_MODE == 3
    REQUIRE(Range(GTOS_SYS_VM_DECOMMIT, handle, 0, Page) == 0);
    const uint8_t value = *(volatile uint8_t*)base; (void)value;
    Fail(__LINE__);
#elif GTOS_VM_PROBE_MODE == 4
    REQUIRE(Control(GTOS_SYS_VM_RELEASE, handle) == 0);
    const uint8_t value = *(volatile uint8_t*)base; (void)value;
    Fail(__LINE__);
#elif GTOS_VM_PROBE_MODE == 5
    REQUIRE(All(base + 2 * Page, Page, 0x6B));
    REQUIRE(Control(GTOS_SYS_VM_TRIM, handle, 2 * Page) == 0);
    const uint8_t value = *(volatile uint8_t*)(base + 2 * Page); (void)value;
    Fail(__LINE__);
#else
    for (;;) Call(GTOS_SYS_YIELD); // Kernel RequestExit must defer frame reclamation.
#endif
}
