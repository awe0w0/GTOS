#include "record.h"
#include <process/abi.h>
#include <process/vm_abi.h>
#ifndef GTOS_CLOCK_PROBE_MODE
#define GTOS_CLOCK_PROBE_MODE 0
#endif
#if !defined(__GTOS__) || defined(__linux__) || defined(__unix__) || defined(_WIN32)
#error Clock probe must compile for the freestanding GTOS target
#endif
static_assert(sizeof(void*) == 4, "Clock probe is IA32");
extern "C" {
__attribute__((section(".data.clock_record"), used))
volatile ClockProbeRecord native_clock_record = {
    1, 0, GTOS_CLOCK_PROBE_MODE, 0, 0, 0, 0, 0, 0, 0, 0, {}, {},
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
}
namespace {
    unsigned Call(unsigned call, unsigned arg1 = 0, unsigned arg2 = 0) {
        unsigned b = arg1, c = arg2, d = 0x193E6A52U, s = 0x27182818U, i = 0x31415926U;
        asm volatile("int $0x80" : "+a"(call), "+b"(b), "+c"(c), "+d"(d), "+S"(s), "+D"(i) : : "memory", "cc");
        if (b != arg1 || c != arg2 || d != 0x193E6A52U || s != 0x27182818U || i != 0x31415926U) {
            native_clock_record.error = 0xC101U;
            asm volatile("int $0x80" : : "a"(GTOS_SYS_EXIT), "b"(0xC101U) : "memory", "cc");
            for (;;) asm volatile("ud2");
        }
        ++native_clock_record.abi_register_checks;
        return call;
    }
    void Fail(unsigned code) __attribute__((noreturn));
    void Fail(unsigned code) {
        native_clock_record.error = code;
        Call(GTOS_SYS_EXIT, 0xC1000000U | code);
        for (;;) asm volatile("ud2");
    }
    void Require(bool condition, unsigned code) {
        if (!condition) Fail(code);
        ++native_clock_record.checks;
    }
    void Fill(void* out, unsigned bytes, unsigned char value) {
        volatile unsigned char* p = (volatile unsigned char*)out;
        for (unsigned i = 0; i < bytes; ++i) p[i] = value;
    }
    bool All(const void* in, unsigned bytes, unsigned char value) {
        const volatile unsigned char* p = (const volatile unsigned char*)in;
        for (unsigned i = 0; i < bytes; ++i) if (p[i] != value) return false;
        return true;
    }
    void Copy(volatile void* out, const void* in, unsigned bytes) {
        volatile unsigned char* d = (volatile unsigned char*)out;
        const unsigned char* s = (const unsigned char*)in;
        for (unsigned i = 0; i < bytes; ++i) d[i] = s[i];
    }
    void Metadata(const GtosClockReadResult& value) {
        Require(value.version == 1 && value.clock_id == GTOS_CLOCK_ID_MONOTONIC
            && value.unit == GTOS_CLOCK_UNIT_MICROSECONDS && value.source == GTOS_CLOCK_SOURCE_PIT_DELIVERED_IRQ
            && value.capabilities == GTOS_CLOCK_REQUIRED_CAPABILITIES && value.resolution_us == 10000
            && value.pit_input_hz == 1193182 && value.pit_divisor == 11931, 1);
    }
    GtosClockReadRequest Request(unsigned out) {
        GtosClockReadRequest r = {1, GTOS_CLOCK_ID_MONOTONIC, 0, out, 48};
        return r;
    }
    unsigned Read(GtosClockReadResult& out) {
        GtosClockReadRequest r = Request((unsigned)&out);
        return Call(GTOS_SYS_CLOCK_READ, (unsigned)&r, sizeof(r));
    }
    void Keep() {
        GtosVmReserveResult out = {};
        GtosVmReserveRequest reserve = {1, 8192, 4096, 0x80000000U, (unsigned)&out};
        Require(Call(GTOS_SYS_VM_RESERVE, (unsigned)&reserve, sizeof(reserve)) == 0
            && out.handle && out.base == 0x80000000U, 2);
        GtosVmRangeRequest commit = {1, out.handle, 0, 8192, GTOS_VM_READ_WRITE};
        Require(Call(GTOS_SYS_VM_SET_PERMISSIONS, (unsigned)&commit, sizeof(commit)) == 0, 3);
        native_clock_record.keep_base = out.base;
        native_clock_record.keep_handle = out.handle;
        native_clock_record.keep_pages = 2;
    }
    void Patterns() {
        volatile unsigned char* p = (volatile unsigned char*)native_clock_record.keep_base;
        for (unsigned page = 0; page < 2; ++page)
            for (unsigned byte = 0; byte < 4096; ++byte) p[page * 4096 + byte] = (unsigned char)(byte ^ (page ? 0xA9 : 0x37));
    }
    void Reject(GtosClockReadRequest& request, unsigned bytes, int expected, void* guard, unsigned guardBytes) {
        Fill(guard, guardBytes, 0xA5);
        Require(Call(GTOS_SYS_CLOCK_READ, (unsigned)&request, bytes) == (unsigned)expected, 4);
        Require(All(guard, guardBytes, 0xA5), 5);
        native_clock_record.sentinel_bytes += guardBytes;
        ++native_clock_record.sentinel_checks;
    }
    void Wire() {
        alignas(8) unsigned char guard[64];
        GtosClockReadRequest request = Request((unsigned)guard);
        const unsigned sizes[] = {0, 19, 21, 0xFFFFFFFFU};
        for (unsigned i = 0; i < 4; ++i) Reject(request, sizes[i], GTOS_CLOCK_ERR_BAD_SIZE, guard, sizeof(guard));
        Fill(guard, sizeof(guard), 0xA5);
        Require(Call(GTOS_SYS_CLOCK_READ, 0xFFFFFFFFU, 20) == (unsigned)GTOS_CLOCK_ERR_BAD_ADDRESS
            && All(guard, sizeof(guard), 0xA5), 6);
        Require(Call(GTOS_SYS_CLOCK_READ, 0xFFFFFFFFU, 19) == (unsigned)GTOS_CLOCK_ERR_BAD_SIZE
            && All(guard, sizeof(guard), 0xA5), 26);
        request.version = 0;
        Reject(request, 20, GTOS_CLOCK_ERR_UNSUPPORTED, guard, sizeof(guard));
        request.flags = 1; request.result_va = 0;
        Reject(request, 20, GTOS_CLOCK_ERR_UNSUPPORTED, guard, sizeof(guard)); // Version precedes flags/address.
        request = Request((unsigned)guard); request.flags = 1;
        Reject(request, 20, GTOS_CLOCK_ERR_BAD_SIZE, guard, sizeof(guard));
        const unsigned resultSizes[] = {0, 47, 49, 0xFFFFFFFFU};
        for (unsigned i = 0; i < 4; ++i) {
            request = Request((unsigned)guard); request.result_bytes = resultSizes[i];
            Reject(request, 20, GTOS_CLOCK_ERR_BAD_SIZE, guard, sizeof(guard));
        }
        const unsigned ids[] = {GTOS_CLOCK_ID_REALTIME, GTOS_CLOCK_ID_THREAD_CPU, 0xFFFFFFFFU};
        for (unsigned i = 0; i < 3; ++i) {
            request = Request(0); request.clock_id = ids[i];
            Reject(request, 20, GTOS_CLOCK_ERR_UNSUPPORTED, guard, sizeof(guard));
        }
        const unsigned addresses[] = {0, 0x3FFFFFFFU, 0xC0000000U, 0xFFFFFFF0U, 0x40000000U};
        for (unsigned i = 0; i < 5; ++i) {
            request = Request(addresses[i]);
            Reject(request, 20, GTOS_CLOCK_ERR_BAD_ADDRESS, guard, sizeof(guard));
        }
        const unsigned base = native_clock_record.keep_base, handle = native_clock_record.keep_handle;
        GtosVmRangeRequest permission = {1, handle, 4096, 4096, GTOS_VM_NONE};
        Require(Call(GTOS_SYS_VM_SET_PERMISSIONS, (unsigned)&permission, sizeof(permission)) == 0, 7);
        request = Request(base + 4096 - 24);
        Reject(request, 20, GTOS_CLOCK_ERR_BAD_ADDRESS, (void*)(base + 4096 - 24), 24);
        permission.protection = GTOS_VM_READ;
        Require(Call(GTOS_SYS_VM_SET_PERMISSIONS, (unsigned)&permission, sizeof(permission)) == 0, 8);
        Reject(request, 20, GTOS_CLOCK_ERR_BAD_ADDRESS, (void*)(base + 4096 - 24), 24);
        permission.protection = GTOS_VM_READ_WRITE;
        Require(Call(GTOS_SYS_VM_SET_PERMISSIONS, (unsigned)&permission, sizeof(permission)) == 0, 9);
        Fill((void*)base, 8192, 0xA5);
        Require(Call(GTOS_SYS_CLOCK_READ, (unsigned)&request, 20) == 0, 10);
        GtosClockReadResult crossed = {};
        Copy(&crossed, (const void*)(base + 4096 - 24), sizeof(crossed)); Metadata(crossed);
        Require(All((const void*)(base + 4096 - 32), 8, 0xA5)
            && All((const void*)(base + 4096 + 24), 8, 0xA5), 11);
        GtosVmRangeRequest decommit = {1, handle, 4096, 4096, 0};
        Require(Call(GTOS_SYS_VM_DECOMMIT, (unsigned)&decommit, sizeof(decommit)) == 0, 12);
        Reject(request, 20, GTOS_CLOCK_ERR_BAD_ADDRESS, (void*)(base + 4096 - 24), 24);
        Fill(guard, sizeof(guard), 0xA5);
        Require(Call(GTOS_SYS_CLOCK_READ, base + 4096 - 8, 20) == (unsigned)GTOS_CLOCK_ERR_BAD_ADDRESS
            && All(guard, sizeof(guard), 0xA5), 13); // Whole input crosses an unmapped page.
        permission.protection = GTOS_VM_READ_WRITE;
        Require(Call(GTOS_SYS_VM_SET_PERMISSIONS, (unsigned)&permission, sizeof(permission)) == 0, 14);
        request = Request(base);
        Require(Call(GTOS_SYS_CLOCK_READ, (unsigned)&request, 20) == 0, 15); // High unsigned output succeeds.
        request = Request(base + 3);
        Require(Call(GTOS_SYS_CLOCK_READ, (unsigned)&request, 20) == 0, 27); // Unaligned checked-copy wire.
        static const GtosClockReadRequest readonlyRequest = {1, 1, 0, 0x80000040U, 48};
        Require(Call(GTOS_SYS_CLOCK_READ, (unsigned)&readonlyRequest, 20) == 0, 28);
        const unsigned shifts[] = {0, 4, 8};
        for (unsigned i = 0; i < 3; ++i) {
            alignas(8) unsigned char overlap[80]; Fill(overlap, sizeof(overlap), 0xA5);
            request = Request((unsigned)overlap + 8 + shifts[i]);
            Copy(overlap + 8, &request, sizeof(request));
            Require(Call(GTOS_SYS_CLOCK_READ, (unsigned)overlap + 8, 20) == 0, 16);
            GtosClockReadResult result = {};
            Copy(&result, overlap + 8 + shifts[i], sizeof(result)); Metadata(result);
            Require(All(overlap, 8, 0xA5) && All(overlap + 8 + shifts[i] + 48,
                sizeof(overlap) - 8 - shifts[i] - 48, 0xA5), 17);
        }
        native_clock_record.sentinel_before = native_clock_record.sentinel_after = 0xA5A5A5A5U;
    }
}
extern "C" void NativeEntry() {
    Keep(); Patterns();
    GtosClockReadResult first = {}, last = {};
    native_clock_record.first_result = Read(first); Require(native_clock_record.first_result == 0, 18); Metadata(first);
    Copy(&native_clock_record.first, &first, sizeof(first));
    Copy(&native_clock_record.last, &first, sizeof(first));
    native_clock_record.legacy_first = native_clock_record.legacy_last = Call(GTOS_SYS_TICKS);
    native_clock_record.stage = 1;
    if (GTOS_CLOCK_PROBE_MODE == 1) { *(volatile unsigned*)GTOS_CLOCK_PROBE_GUARD_VA = 0xBAD; Fail(19); }
    if (GTOS_CLOCK_PROBE_MODE == 2) { for (;;) Call(GTOS_SYS_YIELD); }
    if (GTOS_CLOCK_PROBE_MODE == 3) {
        for (;;) {
            Fill(&last, sizeof(last), 0xA5);
            const unsigned result = Read(last);
            if (result == (unsigned)GTOS_CLOCK_ERR_OVERFLOW) {
                Require(All(&last, sizeof(last), 0xA5), 20);
                native_clock_record.error = native_clock_record.last_result = result;
                ++native_clock_record.clock_failures;
                for (unsigned i = 0; i < 16; ++i) {
                    Require(Read(last) == result && All(&last, sizeof(last), 0xA5), 21);
                    ++native_clock_record.sentinel_checks; native_clock_record.sentinel_bytes += sizeof(last);
                }
                break;
            }
            Require(result == 0, 22); Metadata(last);
            Copy(&native_clock_record.last, &last, sizeof(last)); Call(GTOS_SYS_YIELD);
        }
    } else {
        Wire();
        for (unsigned i = 0; i < 32; ++i) {
            Require(Read(last) == 0, 23); Metadata(last);
            Require(last.microseconds >= first.microseconds && last.delivered_ticks >= first.delivered_ticks, 24);
            Copy(&native_clock_record.last, &last, sizeof(last)); Call(GTOS_SYS_YIELD);
        }
        const unsigned scenario = native_clock_record.scenario;
        if (scenario >= 1 && scenario <= 3) {
            const unsigned long long boundary = scenario == 1 ? 0x80000000ULL : 0x100000000ULL;
            Require((scenario == 3 ? first.microseconds : first.delivered_ticks) < boundary, 26);
            while ((scenario == 3 ? last.microseconds : last.delivered_ticks) < boundary) {
                Require(Read(last) == 0, 27); Metadata(last);
                Copy(&native_clock_record.last, &last, sizeof(last)); Call(GTOS_SYS_YIELD);
            }
        }
        Require(last.microseconds > first.microseconds && last.delivered_ticks > first.delivered_ticks, 25);
        native_clock_record.elapsed_us = last.microseconds - first.microseconds;
        Patterns();
    }
    native_clock_record.legacy_last = Call(GTOS_SYS_TICKS);
    native_clock_record.stage = 2;
    const unsigned start = Call(GTOS_SYS_TICKS);
    while ((unsigned)(Call(GTOS_SYS_TICKS) - start) < 12) Call(GTOS_SYS_YIELD);
    Call(GTOS_SYS_EXIT, 0);
    for (;;) asm volatile("ud2");
}
