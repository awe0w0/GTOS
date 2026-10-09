#include "record.h"
#include "clock-gettime-internal.h"
#include "heap.h"
#include "hwy/timer.h"
#include "src/base/platform/gtos-native-services.h"
#include "src/base/platform/gtos-native-api/vm_abi.h"
#include <stdlib.h>
extern "C" {
__attribute__((section(".data.utc_record"), used))
volatile ClockRecord native_clock_record = {1, GTOS_C_CLOCK_MODE, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, {}};
}
static int Call(unsigned operation, const void* request, unsigned bytes) {
    asm volatile("int $0x80" : "+a"(operation)
        : "b"(static_cast<unsigned>(reinterpret_cast<uintptr_t>(request))), "c"(bytes) : "memory", "cc");
    return static_cast<int>(operation);
}
static void Fail(unsigned error) __attribute__((noreturn));
static void Fail(unsigned error) {
    native_clock_record.error = error;
    Call(GTOS_SYS_EXIT, reinterpret_cast<void*>(0x4C000000U | error), 0);
    __builtin_trap();
}
static void Require(bool condition, unsigned error) {
    if (!condition) Fail(error);
    native_clock_record.checks = native_clock_record.checks + 1;
}
static void Keep() {
    GtosVmReserveResult result = {};
    GtosVmReserveRequest request = {1, 8192, 4096, 0, static_cast<unsigned>(reinterpret_cast<uintptr_t>(&result))};
    Require(Call(GTOS_SYS_VM_RESERVE, &request, sizeof(request)) == 0, 1);
    Require(result.version == 1 && result.base == 0x80000000U && result.length == 8192 && result.page_size == 4096 && result.handle, 2);
    GtosVmRangeRequest range = {1, result.handle, 0, 8192, GTOS_VM_READ_WRITE};
    Require(Call(GTOS_SYS_VM_SET_PERMISSIONS, &range, sizeof(range)) == 0, 3);
    native_clock_record.base = result.base; native_clock_record.handle = result.handle;
    volatile unsigned char* p = reinterpret_cast<volatile unsigned char*>(result.base);
    for (unsigned i = 0; i < 8192; ++i) p[i] = static_cast<unsigned char>(i ^ 0x5DU);
}
static bool Same(timespec a, timespec b) { return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec; }
static void Validators() {
    GtosClockReadResult mono = {1, 1, 1, 1, 7, 10000, 0, 0, 1193182, 11931};
    GtosRealtimeReadResult utc = {1, 1, 1, 15, 10000, 1000000, 0, 0, 0};
    const unsigned long long values[] = {0, 1, 999999, 1000000, 1000001, 0xffffffffULL, 0x100000000ULL,
        0x7ffffffffffffffdULL, 0x7ffffffffffffffeULL, 0x7fffffffffffffffULL, 0x8000000000000000ULL, ~0ULL};
    for (unsigned long long micros : values) {
        mono.microseconds = utc.microseconds = micros;
        timespec m = {}, u = {};
        Require(!gtos_clock::Decode(mono, m) && !gtos_clock::Decode(utc, u) && Same(m, u), 4);
        Require(m.tv_sec >= 0 && m.tv_nsec >= 0 && m.tv_nsec < 1000000000LL &&
            static_cast<unsigned long long>(m.tv_sec) * 1000000ULL +
            static_cast<unsigned long long>(m.tv_nsec / 1000) == micros && m.tv_nsec % 1000 == 0, 5);
    }
    const unsigned indexes[] = {0, 1, 2, 3, 4, 5, 10, 11};
    for (unsigned index : indexes) for (unsigned change = 0; change < 3; ++change) {
        GtosClockReadResult value = mono;
        unsigned* word = reinterpret_cast<unsigned*>(&value) + index;
        *word = change == 0 ? 0U : (change == 1 ? *word ^ 0x80000000U : ~0U);
        timespec destination = {51, 52};
        Require(gtos_clock::Decode(value, destination) == EIO && Same(destination, {51, 52}), 6);
        native_clock_record.rejected = native_clock_record.rejected + 1;
    }
    for (unsigned index = 0; index < 6; ++index) for (unsigned change = 0; change < 3; ++change) {
        GtosRealtimeReadResult value = utc; unsigned* word = reinterpret_cast<unsigned*>(&value) + index;
        *word = change == 0 ? 0U : (change == 1 ? *word ^ 0x80000000U : ~0U);
        timespec destination = {51, 52};
        Require(gtos_clock::Decode(value, destination) == EIO && Same(destination, {51, 52}), 7);
        native_clock_record.rejected = native_clock_record.rejected + 1;
    }
    Require(gtos_clock::Error(-22) == EINVAL && gtos_clock::Error(-14) == EFAULT &&
        gtos_clock::Error(-38) == ENOSYS && gtos_clock::Error(-75) == EOVERFLOW &&
        gtos_clock::Error(1) == EIO && gtos_clock::Error(-1) == EIO, 8);
}
static void Sample(unsigned kind) {
    const unsigned index = native_clock_record.count;
    Require(index < 6, 9); native_clock_record.pending = kind;
    errno = 0x5522; timespec ts = {51, 52}; int status = 0;
    if (kind == 3) ts = {static_cast<long long>(hwy::timer::Start()), 0};
    else status = clock_gettime(kind == 1 ? CLOCK_MONOTONIC : CLOCK_REALTIME, &ts);
    native_clock_record.samples[index].kind = kind;
    native_clock_record.samples[index].status = static_cast<unsigned>(status);
    native_clock_record.samples[index].seconds = ts.tv_sec;
    native_clock_record.samples[index].nanoseconds = ts.tv_nsec;
    native_clock_record.samples[index].error = static_cast<unsigned>(errno);
    native_clock_record.count = index + 1; native_clock_record.pending = 9;
    if (kind == 2 && GTOS_C_CLOCK_MODE == 3) Require(status == -1 && errno == ENOSYS && Same(ts, {51, 52}), 10);
    else Require(status == 0 && errno == 0x5522 && ts.tv_sec >= 0 && ts.tv_nsec >= 0 && ts.tv_nsec < 1000000000LL, 11);
}
extern "C" void NativeEntry() {
    Keep(); Validators();
    Require(errno == 0, 27);
    errno = 0x1357;
    int* error_address = __llvm_libc_errno();
    Require(error_address == __llvm_libc_errno() && *error_address == 0x1357, 12);
    native_clock_record.errno_va = static_cast<unsigned>(reinterpret_cast<uintptr_t>(error_address));
    GtosVmRegionInfo heap = {}; Require(gtos_native_heap_query(&heap) == 1 && heap.length == 65536, 13);
    Require(reinterpret_cast<uintptr_t>(error_address) >= heap.base &&
        reinterpret_cast<uintptr_t>(error_address) <= heap.base + heap.length - sizeof(int), 14);
    native_clock_record.heap_base = heap.base; native_clock_record.heap_handle = heap.handle; native_clock_record.heap_bytes = heap.length;
    timespec sentinel = {51, 52};
    Require(clock_gettime(-1, &sentinel) == -1 && errno == EINVAL && Same(sentinel, {51, 52}), 15);
    Require(clock_gettime(CLOCK_MONOTONIC, nullptr) == -1 && errno == EFAULT, 16);
    errno = 0x1357; void* pointer = reinterpret_cast<void*>(0x40000000U);
    Require(posix_memalign(&pointer, 4, 65536) == ENOMEM && errno == 0x1357 &&
        pointer == reinterpret_cast<void*>(0x40000000U), 17);
    Require(malloc(65536) == nullptr && errno == ENOMEM, 18);
    Require(calloc(~0U, 2) == nullptr && errno == ENOMEM, 19);
    errno = 0x1357; pointer = malloc(48);
    Require(pointer && errno == 0x1357, 20); free(pointer); Require(errno == 0x1357, 21);
    native_clock_record.stage = 1;
    Sample(1); Sample(2); Sample(3);
    const long long begin = native_clock_record.samples[0].seconds * 1000000000LL + native_clock_record.samples[0].nanoseconds;
    timespec now = {};
    do { Require(clock_gettime(CLOCK_MONOTONIC, &now) == 0, 22); }
    while (now.tv_sec * 1000000000LL + now.tv_nsec - begin < 30000 * 1000LL);
    Sample(1); Sample(2);
    native_clock_record.errno_value = static_cast<unsigned>(*error_address);
    // A complete timespec may straddle two owned writable pages.
    timespec* crossing = reinterpret_cast<timespec*>(0x80000ff8U);
    Require(clock_gettime(CLOCK_MONOTONIC, crossing) == 0 && crossing->tv_nsec >= 0 && crossing->tv_nsec < 1000000000LL, 23);
    volatile unsigned char* p = reinterpret_cast<volatile unsigned char*>(0x80000000U);
    for (unsigned i = 0; i < 8192; ++i) {
        if (i >= 4088 && i < 4104) continue;
        Require(p[i] == static_cast<unsigned char>(i ^ 0x5DU), 24);
        native_clock_record.sentinel_checks = native_clock_record.sentinel_checks + 1;
    }
    native_clock_record.stage = 2;
#if GTOS_C_CLOCK_MODE == 1
    clock_gettime(CLOCK_MONOTONIC, reinterpret_cast<timespec*>(0xBFFFCFFCU));
    Fail(25);
#elif GTOS_C_CLOCK_MODE == 2
    for (;;) asm volatile("" : : : "memory");
#else
    Call(GTOS_SYS_EXIT, nullptr, 0); Fail(26);
#endif
}
