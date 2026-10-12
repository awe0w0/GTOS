#include <chrono>
#include <time.h>
#include "src/base/platform/gtos-native-services.h"
#include "src/base/platform/gtos-native-api/vm_abi.h"
#include "heap.h"
#include "record.h"

static_assert(std::chrono::steady_clock::is_steady, "Actual monotonic clock");
static_assert(std::chrono::steady_clock::period::num == 1 &&
              std::chrono::steady_clock::period::den == 1000000000 &&
              std::chrono::system_clock::period::num == 1 &&
              std::chrono::system_clock::period::den == 1000000, "Pinned clock units");
extern "C" {
volatile NativeChronoRecord native_chrono_record
    __attribute__((section(".data.chrono_record"), used)) = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
}
static long long Values[4];
namespace {
unsigned Call(unsigned number, const void* request, unsigned bytes) {
    asm volatile("int $0x80" : "+a"(number)
        : "b"((unsigned)request), "c"(bytes) : "memory", "cc");
    return number;
}
[[noreturn]] void Exit(unsigned result) {
    Call(GTOS_SYS_EXIT, (const void*)result, 0);
    __builtin_trap();
}
void Check(bool value, unsigned error) {
    native_chrono_record.checks = native_chrono_record.checks + 1;
    if (!value) { native_chrono_record.error = error; Exit(99); }
}
bool EmptyHeap() {
    GtosVmRegionInfo info = {};
    return gtos_native_heap_query(&info) == 0;
}
void Sample(unsigned kind) {
    unsigned index = native_chrono_record.mono + native_chrono_record.wall;
    native_chrono_record.pending = kind;
    native_chrono_record.calls = native_chrono_record.calls + 1;
    Values[index] = kind == 1 ? std::chrono::steady_clock::now().time_since_epoch().count()
                             : std::chrono::system_clock::now().time_since_epoch().count();
    Check(Values[index] >= 0, 100 + index);
    if (kind == 1) native_chrono_record.mono = native_chrono_record.mono + 1;
    else native_chrono_record.wall = native_chrono_record.wall + 1;
    native_chrono_record.pending = 0;
}
}
extern "C" void NativeEntry() {
    Check(v8::base::gtos::CurrentProcessInfo().maximum_parallel_threads == 1, 1);
    GtosVmReserveResult result = {};
    GtosVmReserveRequest reserve = {1, 4096, 4096, 0x80000000U, (unsigned)&result};
    Check(Call(GTOS_SYS_VM_RESERVE, &reserve, sizeof(reserve)) == 0, 2);
    Check(result.version == 1 && result.handle && result.base == 0x80000000U
        && result.length == 4096 && result.page_size == 4096, 3);
    GtosVmRangeRequest commit = {1, result.handle, 0, 4096, GTOS_VM_READ_WRITE};
    Check(Call(GTOS_SYS_VM_SET_PERMISSIONS, &commit, sizeof(commit)) == 0, 4);
    Check(*(volatile unsigned*)result.base == 0, 5);
    *(volatile unsigned*)result.base = 0xC011CA11U;
    Check(EmptyHeap(), 6);
    Check(sizeof(Values) == 32 && sizeof(time_t) == 8, 7);
    native_chrono_record.region = result.handle;
    native_chrono_record.base = result.base;
    native_chrono_record.length = result.length;
    native_chrono_record.pointer = (unsigned)Values;
    native_chrono_record.objectBytes = sizeof(Values);
    native_chrono_record.heapEmpty = 1;
    native_chrono_record.stage = 1;
    Sample(1); Sample(2);
    native_chrono_record.pending = 9;
    timespec first = {}, now = {};
    Check(clock_gettime(CLOCK_MONOTONIC, &first) == 0, 200);
    do {
        Call(GTOS_SYS_YIELD, nullptr, 0);
        Check(clock_gettime(CLOCK_MONOTONIC, &now) == 0, 201);
    } while ((now.tv_sec - first.tv_sec) * 1000000000LL + now.tv_nsec - first.tv_nsec < 30000000);
    native_chrono_record.pending = 0;
    Sample(1); Sample(2);
    Check(Values[2] - Values[0] >= 30000000, 202);
    Check(Values[3] - Values[1] >= 30000, 203);
    auto wall = std::chrono::system_clock::time_point(std::chrono::system_clock::duration(Values[3]));
    time_t seconds = std::chrono::system_clock::to_time_t(wall);
    Check(seconds == Values[3] / 1000000, 204);
    Check(std::chrono::system_clock::from_time_t(seconds).time_since_epoch().count() == seconds * 1000000, 205);
    Check(EmptyHeap() && native_chrono_record.mono == 2 && native_chrono_record.wall == 2, 206);
    native_chrono_record.stage = 2;
    unsigned mode = native_chrono_record.mode;
    if (mode == 1) {
        native_chrono_record.pending = 9;
        clock_gettime(CLOCK_MONOTONIC, (timespec*)0xBFFFCFFCU);
        Exit(98);
    }
    if (mode == 2) {
        long long last = Values[2];
        native_chrono_record.pending = 9;
        for (;;) {
            native_chrono_record.calls = native_chrono_record.calls + 1;
            long long current = std::chrono::steady_clock::now().time_since_epoch().count();
            Check(current >= last, 207);
            last = current;
            native_chrono_record.cycles = native_chrono_record.cycles + 1;
        }
    }
    if (mode == 3) {
        native_chrono_record.pending = 4;
        native_chrono_record.calls = native_chrono_record.calls + 1;
        (void)std::chrono::steady_clock::now();
        Exit(98);
    }
    Exit(0);
}
