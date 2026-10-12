#include <__locale_dir/support/llvm_libc.h>
#include "src/base/platform/gtos-native-services.h"
#include "src/base/platform/gtos-native-api/vm_abi.h"
#include "heap.h"
#include "record.h"
#include "cases.h"

static_assert(sizeof(tm) == 36 && sizeof(time_t) == 8, "Pinned IA32 time ABI");
extern "C" {
volatile NativeTimeFormatRecord native_time_format_record
    __attribute__((section(".data.time_format_record"), used)) = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
}
static tm Date = {5,7,23,29,1,124,4,59,0};
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
    native_time_format_record.checks = native_time_format_record.checks + 1;
    if (!value) { native_time_format_record.error = error; Exit(99); }
}
bool EmptyHeap() {
    GtosVmRegionInfo info = {};
    return gtos_native_heap_query(&info) == 0;
}
unsigned Length(const char* text) {
    unsigned size = 0;
    while (text[size]) ++size;
    return size;
}
bool Equal(const char* a, const char* b, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) if (a[i] != b[i]) return false;
    return true;
}
size_t Format(char* out, size_t size, const char* format, const tm* value) {
    native_time_format_record.calls = native_time_format_record.calls + 1;
    return std::__locale::__strftime(out, size, format, value, nullptr);
}
void Guards(const unsigned char* data, unsigned first, unsigned last, unsigned size, unsigned error) {
    for (unsigned i = 0; i < first; ++i) Check(data[i] == 0xA5, error);
    for (unsigned i = last; i < size; ++i) Check(data[i] == 0xA5, error);
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
    Check(sizeof(Date) == 36 && sizeof(time_t) == 8, 7);
    native_time_format_record.region = result.handle;
    native_time_format_record.base = result.base;
    native_time_format_record.length = result.length;
    native_time_format_record.pointer = (unsigned)&Date;
    native_time_format_record.objectBytes = sizeof(Date);
    native_time_format_record.heapEmpty = 1;
    native_time_format_record.stage = 1;
    unsigned mode = native_time_format_record.mode;
    char output[128];
    if (mode == 1) { Format((char*)0xBFFFCFF0U, 32, "%F", &Date); Exit(98); }
    if (mode == 2) { Format(output, sizeof(output), (const char*)0xBFFFCFF0U, &Date); Exit(98); }
    if (mode == 3) { Format(output, sizeof(output), "%S", (const tm*)0xBFFFCFF0U); Exit(98); }
    if (mode == 4) {
        for (;;) {
            Format(output, sizeof(output), "%c %G-W%V %s", &Date);
            native_time_format_record.cycles = native_time_format_record.cycles + 1;
        }
    }
    for (const FormatCase& item : Cases) {
        unsigned char guarded[136];
        for (unsigned char& byte : guarded) byte = 0xA5;
        unsigned expected = Length(item.expected);
        size_t length = Format((char*)guarded + 4, 128, item.format, &item.value);
        Check(length == expected && Equal((char*)guarded + 4, item.expected, expected + 1), 1000 + native_time_format_record.masks);
        Guards(guarded, 4, 4 + expected + 1, sizeof(guarded), 1100);
        native_time_format_record.masks = native_time_format_record.masks + 1;
    }
    const char* expected = "2024-02-29";
    for (unsigned capacity = 0; capacity <= 12; ++capacity) {
        unsigned char guarded[24];
        for (unsigned char& byte : guarded) byte = 0xA5;
        size_t length = Format((char*)guarded + 4, capacity, "%F", &Date);
        Check(length == (capacity > 10 ? 10U : 0U), 2000 + capacity);
        if (capacity) {
            unsigned stored = capacity - 1 < 10 ? capacity - 1 : 10;
            Check(Equal((char*)guarded + 4, expected, stored) && guarded[4 + stored] == 0, 2100 + capacity);
        }
        Guards(guarded, 4, 4 + capacity, sizeof(guarded), 2200 + capacity);
        native_time_format_record.rejected = native_time_format_record.rejected + 1;
    }
    Check(Format(nullptr, 0, "%F", &Date) == 0, 3000);
    Check(Format((char*)0xBFFFCFF0U, 0, "%F", &Date) == 0, 3001);
    char empty = 'x';
    Check(Format(&empty, 0, "", &Date) == 0 && empty == 'x', 3002);
    Check(Format(&empty, 1, "", &Date) == 0 && empty == 0, 3003);
    unsigned char wide[20];
    for (unsigned char& byte : wide) byte = 0xA5;
    Check(Format((char*)wide + 4, 12, "%2000Y", &Date) == 0 && wide[15] == 0, 3004);
    for (unsigned i = 4; i < 15; ++i) Check(wide[i] == '0', 3005);
    Guards(wide, 4, 16, sizeof(wide), 3006);
    char* edge = (char*)(result.base + 4096 - 11);
    Check(Format(edge, 11, "%F", &Date) == 10 && Equal(edge, expected, 11), 3007);
    Check(Format(edge, 10, "%F", &Date) == 0 && edge[9] == 0, 3008);
    Check(EmptyHeap() && native_time_format_record.masks == ExpectedFormatCases
        && native_time_format_record.rejected == ExpectedCapacityCases
        && native_time_format_record.calls == ExpectedFormatCases + ExpectedCapacityCases + 7, 4000);
    native_time_format_record.stage = 2;
    Exit(0);
}
