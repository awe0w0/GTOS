#include "record.h"
#include <process/abi.h>
#include <process/vm_abi.h>
#ifndef GTOS_REALTIME_PROBE_MODE
#define GTOS_REALTIME_PROBE_MODE 0
#endif
static_assert(sizeof(void*) == 4, "Realtime probe is IA32");
extern "C" {
__attribute__((section(".data.realtime_record"), used))
volatile RealtimeProbeRecord native_realtime_record = {1, GTOS_REALTIME_PROBE_MODE, 0, 0, 0, 0, 0, 0, {}, {}, {}, {}, 0, 0, 0, {}};
}
namespace {
    void Fail(unsigned code) __attribute__((noreturn));
    void Fail(unsigned code) {
        native_realtime_record.error = code;
        asm volatile("int $0x80" : : "a"(GTOS_SYS_EXIT), "b"(0xC2000000U | code) : "memory", "cc");
        for (;;) asm volatile("ud2");
    }
    unsigned Call(unsigned call, unsigned arg1 = 0, unsigned arg2 = 0) {
        unsigned b = arg1, c = arg2, d = 0x193E6A52U, s = 0x27182818U, i = 0x31415926U;
        asm volatile("int $0x80" : "+a"(call), "+b"(b), "+c"(c), "+d"(d), "+S"(s), "+D"(i) : : "memory", "cc");
        if (b != arg1 || c != arg2 || d != 0x193E6A52U || s != 0x27182818U || i != 0x31415926U) Fail(1);
        ++native_realtime_record.abi;
        return call;
    }
    void Require(bool value, unsigned code) {
        if (!value) Fail(code);
        ++native_realtime_record.checks;
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
    GtosRealtimeReadRequest Request(unsigned out) {
        const GtosRealtimeReadRequest value = {1, 0, out, 48}; return value;
    }
    void Read(GtosRealtimeReadResult& out) {
        GtosRealtimeReadRequest request = Request((unsigned)&out);
        Require(Call(GTOS_SYS_REALTIME_READ, (unsigned)&request, 16) == 0, 2);
        Require(out.version == 1 && out.unit == 1 && out.source == 1 && out.capabilities == 15
            && out.resolution_us == 10000 && out.anchor_uncertainty_us == 1000000, 3);
    }
    void Monotonic(GtosClockReadResult& out) {
        GtosClockReadRequest request = {1, 1, 0, (unsigned)&out, 48};
        Require(Call(GTOS_SYS_CLOCK_READ, (unsigned)&request, 20) == 0
            && out.version == 1 && out.clock_id == 1 && out.source == 1
            && out.capabilities == 7 && out.resolution_us == 10000
            && out.pit_input_hz == 1193182 && out.pit_divisor == 11931, 4);
    }
    void Reject(GtosRealtimeReadRequest& request, unsigned bytes, int expected, void* out, unsigned count) {
        Fill(out, count, 0xA5);
        Require(Call(GTOS_SYS_REALTIME_READ, (unsigned)&request, bytes) == (unsigned)expected, 5);
        Require(All(out, count, 0xA5), 6);
        ++native_realtime_record.rejected; native_realtime_record.sentinels += count;
    }
    void Reserve() {
        GtosVmReserveResult out = {};
        GtosVmReserveRequest request = {1, 8192, 4096, 0x80000000U, (unsigned)&out};
        Require(Call(GTOS_SYS_VM_RESERVE, (unsigned)&request, sizeof(request)) == 0
            && out.base == 0x80000000U && out.handle, 7);
        GtosVmRangeRequest commit = {1, out.handle, 0, 8192, GTOS_VM_READ_WRITE};
        Require(Call(GTOS_SYS_VM_SET_PERMISSIONS, (unsigned)&commit, sizeof(commit)) == 0, 8);
        native_realtime_record.base = out.base; native_realtime_record.handle = out.handle;
    }
    void Wire() {
        unsigned char guard[64];
        GtosRealtimeReadRequest request = Request((unsigned)guard);
        const unsigned sizes[] = {0,15,17,0xFFFFFFFFU};
        for (unsigned i = 0; i < 4; ++i) Reject(request, sizes[i], -22, guard, 64);
        Require(Call(GTOS_SYS_REALTIME_READ, 0xFFFFFFF8U, 16) == (unsigned)-14
            && Call(GTOS_SYS_REALTIME_READ, 0xFFFFFFF8U, 15) == (unsigned)-22, 9);
        request.version = 0; request.flags = 1; request.result_va = 0;
        Reject(request, 16, -38, guard, 64);
        request = Request((unsigned)guard); request.flags = 1;
        Reject(request, 16, -22, guard, 64);
        const unsigned resultSizes[] = {0,47,49,0xFFFFFFFFU};
        for (unsigned i = 0; i < 4; ++i) {
            request = Request((unsigned)guard); request.result_bytes = resultSizes[i];
            Reject(request, 16, -22, guard, 64);
        }
        const unsigned addresses[] = {0,0x3FFFFFFFU,0xC0000000U,0xFFFFFFF0U,0x40000000U,0xBFFFEFE0U};
        for (unsigned i = 0; i < 6; ++i) {
            request = Request(addresses[i]); Reject(request, 16, -14, guard, 64);
        }
        const unsigned base = native_realtime_record.base, handle = native_realtime_record.handle;
        GtosVmRangeRequest permissions = {1,handle,4096,4096,GTOS_VM_NONE};
        Require(Call(GTOS_SYS_VM_SET_PERMISSIONS, (unsigned)&permissions, sizeof(permissions)) == 0, 10);
        request = Request(base + 4096 - 24);
        Reject(request, 16, -14, (void*)(base + 4096 - 24), 24);
        Require(Call(GTOS_SYS_REALTIME_READ, base + 4096 - 8, 16) == (unsigned)-14, 11);
        permissions.protection = GTOS_VM_READ;
        Require(Call(GTOS_SYS_VM_SET_PERMISSIONS, (unsigned)&permissions, sizeof(permissions)) == 0, 12);
        Reject(request, 16, -14, (void*)(base + 4096 - 24), 24);
        permissions.protection = GTOS_VM_READ_WRITE;
        Require(Call(GTOS_SYS_VM_SET_PERMISSIONS, (unsigned)&permissions, sizeof(permissions)) == 0, 13);
        Fill((void*)base, 8192, 0xA5);
        Require(Call(GTOS_SYS_REALTIME_READ, (unsigned)&request, 16) == 0, 14);
        Require(All((void*)base, 4096 - 24, 0xA5) && All((void*)(base + 4096 + 24), 4096 - 24, 0xA5), 15);
        // Both request and output may cross readable/writable page boundaries.
        Copy((void*)(base + 4096 - 8), &request, 16);
        Require(Call(GTOS_SYS_REALTIME_READ, base + 4096 - 8, 16) == 0, 16);
        Fill((void*)base, 8192, 0xA5);
        request = Request(base + 8);
        Copy((void*)(base + 8), &request, 16);
        Require(Call(GTOS_SYS_REALTIME_READ, base + 8, 16) == 0, 17);
        Require(All((void*)base, 8, 0xA5) && All((void*)(base + 56), 8192 - 56, 0xA5), 18);
        GtosRealtimeReadResult overlap;
        Copy(&overlap, (void*)(base + 8), 48);
        Require(overlap.version == 1 && overlap.capabilities == 15, 19);
        // The original monotonic syscall still rejects REALTIME selection.
        GtosClockReadRequest old = {1,GTOS_CLOCK_ID_REALTIME,0,(unsigned)guard,48};
        Fill(guard, 64, 0xA5);
        Require(Call(GTOS_SYS_CLOCK_READ, (unsigned)&old, sizeof(old)) == (unsigned)-38 && All(guard,64,0xA5), 20);
        Fill((void*)base, 8192, 0x37);
    }
}
extern "C" void NativeEntry() {
    if (GTOS_REALTIME_PROBE_MODE == 3) {
        unsigned char guard[64]; GtosRealtimeReadRequest request = Request((unsigned)guard);
        Reject(request, 16, -38, guard, 64);
        GtosClockReadResult mono; Monotonic(mono);
        native_realtime_record.stage = 2; Call(GTOS_SYS_EXIT, 0);
        for (;;) asm volatile("ud2");
    }
    Reserve(); Wire();
    GtosClockReadResult mono; Monotonic(mono); Copy(&native_realtime_record.monoFirst, &mono, 48);
    GtosRealtimeReadResult first, last; Read(first); Copy(&native_realtime_record.first, &first, 48);
    native_realtime_record.stage = 1;
    do { Call(GTOS_SYS_YIELD); Read(last); } while (last.monotonic_microseconds - first.monotonic_microseconds < 30000);
    Require(last.microseconds > first.microseconds && last.delivered_ticks > first.delivered_ticks
        && last.microseconds - first.microseconds == last.monotonic_microseconds - first.monotonic_microseconds, 21);
    Copy(&native_realtime_record.last, &last, 48);
    Monotonic(mono); Copy(&native_realtime_record.monoLast, &mono, 48);
    native_realtime_record.stage = 2;
    if (GTOS_REALTIME_PROBE_MODE == 1) *(volatile unsigned*)0xBFFFC000U = 1;
    if (GTOS_REALTIME_PROBE_MODE == 2) for (;;) Call(GTOS_SYS_YIELD);
    Call(GTOS_SYS_EXIT, 0);
    for (;;) asm volatile("ud2");
}
