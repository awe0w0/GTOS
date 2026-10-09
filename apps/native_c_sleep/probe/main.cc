#include "record.h"
#include "heap.h"
#include "nanosleep-internal.h"
#include "elapsed-cases.h"
#include "src/base/platform/gtos-native-services.h"
#include "src/base/platform/gtos-native-api/vm_abi.h"
#include <time.h>
#include <errno.h>
#include <stdint.h>
extern "C" {
__attribute__((section(".data.sleep_record"), used))
volatile SleepRecord native_sleep_record = {1, GTOS_SLEEP_MODE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, {}};
}
static int Call(unsigned operation, uintptr_t argument, unsigned bytes) {
    asm volatile("int $0x80" : "+a"(operation) : "b"(argument), "c"(bytes) : "memory", "cc");
    return static_cast<int>(operation);
}
static void Require(bool pass, unsigned code) {
    if (!pass) {
        native_sleep_record.error = code;
        Call(GTOS_SYS_EXIT, 0x51000000U | code, 0);
        __builtin_trap();
    }
    native_sleep_record.checks = native_sleep_record.checks + 1;
}
static bool Same(timespec a, timespec b) { return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec; }
static void Sample(unsigned kind, long long nanoseconds) {
    const unsigned index = native_sleep_record.count;
    Require(index < 12, 1);
    timespec* request = reinterpret_cast<timespec*>(0x80000FF8U);
    *request = {0, nanoseconds};
    const timespec saved = *request;
    volatile SleepSample& sample = native_sleep_record.samples[index];
    sample.kind = kind; sample.requested_seconds = 0; sample.requested_nanoseconds = nanoseconds;
    native_sleep_record.active = index + 1; native_sleep_record.marker = 1;
    timespec before = {}, after = {};
    errno = 0x5533;
    Require(clock_gettime(CLOCK_MONOTONIC, &before) == 0, 2);
    native_sleep_record.marker = 2;
    sample.status = static_cast<unsigned>(nanosleep(request, request));
    native_sleep_record.marker = 3;
    Require(clock_gettime(CLOCK_MONOTONIC, &after) == 0 && errno == 0x5533, 3);
    sample.before_seconds = before.tv_sec; sample.before_nanoseconds = before.tv_nsec;
    sample.after_seconds = after.tv_sec; sample.after_nanoseconds = after.tv_nsec;
    Require(sample.status == 0 && Same(*request, saved), 4);
    Require(after.tv_sec * 1000000000LL + after.tv_nsec -
        before.tv_sec * 1000000000LL - before.tv_nsec >= nanoseconds, 5);
    native_sleep_record.count = index + 1; native_sleep_record.active = 0; native_sleep_record.marker = 9;
}
static void Helpers() {
    const timespec invalid[] = {{-1, 0}, {0, -1}, {0, 1000000000LL},
        {0x7fffffffffffffffLL, 0x7fffffffffffffffLL}};
    for (timespec value : invalid) Require(!gtos_sleep::Valid(value), 17);
    for (const ElapsedCase& value : elapsed_cases) {
        // Load through a volatile object so native arithmetic is executed.
        volatile timespec begin = value.begin, now = value.now, duration = value.duration;
        const timespec a = {begin.tv_sec, begin.tv_nsec};
        const timespec b = {now.tv_sec, now.tv_nsec};
        const timespec c = {duration.tv_sec, duration.tv_nsec};
        Require(gtos_sleep::Valid(c), 18);
        Require(gtos_sleep::Elapsed(a, b, c) == value.expected, 19);
    }
}
static void Rejects() {
    timespec remainder = {51, 52};
    Require(nanosleep(nullptr, &remainder) == -1 && errno == EFAULT && Same(remainder, {51, 52}), 6);
    const timespec invalid[] = {{-1, 0}, {0, -1}, {0, 1000000000LL}, {0, 0x7fffffffffffffffLL},
        {0x7fffffffffffffffLL, 1000000000LL}};
    for (timespec value : invalid) {
        const timespec saved = value;
        Require(nanosleep(&value, &value) == -1 && errno == EINVAL && Same(value, saved), 7);
        native_sleep_record.rejected = native_sleep_record.rejected + 1;
    }
    timespec zero = {};
    errno = 0x5533;
    Require(nanosleep(&zero, reinterpret_cast<timespec*>(0xBFFFCFFCU)) == 0 && errno == 0x5533, 8);
    for (unsigned marker = 10; marker <= 12; ++marker) {
        native_sleep_record.marker = marker;
        timespec request = {0, 1000000};
        const timespec saved = request;
        Require(nanosleep(&request, &request) == -1 &&
            errno == (marker == 10 ? ENOSYS : (marker == 11 ? EOVERFLOW : EIO)) && Same(request, saved), 9);
        native_sleep_record.rejected = native_sleep_record.rejected + 1;
    }
    native_sleep_record.marker = 9; errno = 0x5533;
}
extern "C" void NativeEntry() {
    GtosVmReserveResult result = {};
    GtosVmReserveRequest request = {1, 8192, 4096, 0, static_cast<unsigned>(reinterpret_cast<uintptr_t>(&result))};
    Require(Call(GTOS_SYS_VM_RESERVE, reinterpret_cast<uintptr_t>(&request), sizeof(request)) == 0, 10);
    Require(result.base == 0x80000000U && result.length == 8192 && result.handle && result.page_size == 4096, 11);
    GtosVmRangeRequest permissions = {1, result.handle, 0, 8192, GTOS_VM_READ_WRITE};
    Require(Call(GTOS_SYS_VM_SET_PERMISSIONS, reinterpret_cast<uintptr_t>(&permissions), sizeof(permissions)) == 0, 12);
    native_sleep_record.base = result.base; native_sleep_record.handle = result.handle;
    volatile unsigned char* keep = reinterpret_cast<volatile unsigned char*>(result.base);
    for (unsigned i = 0; i < 8192; ++i) keep[i] = static_cast<unsigned char>(i ^ 0x5DU);
    Require(errno == 0, 13);
    native_sleep_record.errno_va = static_cast<unsigned>(reinterpret_cast<uintptr_t>(&errno));
    Helpers();
    Rejects();
    native_sleep_record.stage = 1;
    const long long durations[] = {0, 1, 999, 1000, 1001, 999999, 10000000, 10000001, 25000000};
    for (long long duration : durations) Sample(1, duration);
    Sample(1, 0); Sample(1, 1); Sample(1, 15000001);
    GtosVmRegionInfo heap = {};
    Require(gtos_native_heap_query(&heap) == 1 && heap.length == 65536, 15);
    native_sleep_record.heap_base = heap.base; native_sleep_record.heap_bytes = heap.length;
    native_sleep_record.errno_value = static_cast<unsigned>(errno);
    for (unsigned i = 0; i < 8192; ++i) {
        if (i >= 4088 && i < 4104) continue;
        Require(keep[i] == static_cast<unsigned char>(i ^ 0x5DU), 16);
    }
    native_sleep_record.stage = 2;
    if (GTOS_SLEEP_MODE == 1) {
        native_sleep_record.stage = 3;
        nanosleep(reinterpret_cast<const timespec*>(0xBFFFCFFCU), nullptr);
    } else if (GTOS_SLEEP_MODE == 2) {
        timespec large = {0x7fffffffffffffffLL, 999999999};
        native_sleep_record.wait_va = static_cast<unsigned>(reinterpret_cast<uintptr_t>(&large));
        native_sleep_record.stage = 3;
        nanosleep(&large, &large);
    } else if (GTOS_SLEEP_MODE == 3) {
        timespec large = {0x7fffffffffffffffLL, 0};
        native_sleep_record.wait_va = static_cast<unsigned>(reinterpret_cast<uintptr_t>(&large));
        native_sleep_record.stage = 3;
        nanosleep(&large, &large);
    } else Call(GTOS_SYS_EXIT, 0, 0);
    native_sleep_record.error = 90;
    Call(GTOS_SYS_EXIT, 90, 0);
    __builtin_trap();
}
