#include <__locale_dir/support/llvm_libc.h>
#include <wchar.h>
#include <stdlib.h>
#include <errno.h>
#include "src/base/platform/gtos-native-services.h"
#include "src/base/platform/gtos-native-api/vm_abi.h"
#include "heap.h"
#include "record.h"

static_assert(sizeof(mbstate_t) == 8 && alignof(mbstate_t) == 4, "Pinned state ABI");
static_assert(__builtin_offsetof(mbstate_t, __field1) == 0
    && __builtin_offsetof(mbstate_t, __field2) == 4
    && __builtin_offsetof(mbstate_t, __field3) == 5, "Pinned state fields");
extern "C" {
volatile NativeWideRecord native_wide_record
    __attribute__((section(".data.wide_record"), used)) = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
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
    native_wide_record.checks = native_wide_record.checks + 1;
    if (!value) { native_wide_record.error = error; Exit(99); }
}
bool EmptyHeap() {
    GtosVmRegionInfo info = {};
    return gtos_native_heap_query(&info) == 0;
}
bool Empty(const mbstate_t& state) {
    return state.__field1 == 0 && state.__field2 == 0 && state.__field3 == 0;
}
size_t Decode(wchar_t* dst, const char** src, size_t n, size_t len, mbstate_t* state, bool bounded = true) {
    native_wide_record.calls = native_wide_record.calls + 1;
    return bounded ? mbsnrtowcs(dst, src, n, len, state) : mbsrtowcs(dst, src, len, state);
}
size_t Encode(char* dst, const wchar_t** src, size_t n, size_t len, mbstate_t* state) {
    native_wide_record.calls = native_wide_record.calls + 1;
    return wcsnrtombs(dst, src, n, len, state);
}
size_t Read(wchar_t* dst, const char* src, size_t n, mbstate_t* state) {
    native_wide_record.calls = native_wide_record.calls + 1;
    return mbrtowc(dst, src, n, state);
}
size_t Width(const char* src, size_t n, mbstate_t* state) {
    native_wide_record.calls = native_wide_record.calls + 1;
    return mbrlen(src, n, state);
}
int Legacy(wchar_t* dst, const char* src, size_t n) {
    native_wide_record.calls = native_wide_record.calls + 1;
    return mbtowc(dst, src, n);
}
size_t Write(char* dst, wchar_t value, mbstate_t* state) {
    native_wide_record.calls = native_wide_record.calls + 1;
    return wcrtomb(dst, value, state);
}
struct Valid { unsigned scalar, size; unsigned char bytes[4]; };
const Valid Cases[] = {
    {0,1,{0}}, {0x7F,1,{0x7F}}, {0x80,2,{0xC2,0x80}}, {0xA2,2,{0xC2,0xA2}},
    {0x7FF,2,{0xDF,0xBF}}, {0x800,3,{0xE0,0xA0,0x80}}, {0xD7FF,3,{0xED,0x9F,0xBF}},
    {0xE000,3,{0xEE,0x80,0x80}}, {0xFFFF,3,{0xEF,0xBF,0xBF}},
    {0x10000,4,{0xF0,0x90,0x80,0x80}}, {0x1F600,4,{0xF0,0x9F,0x98,0x80}},
    {0x10FFFF,4,{0xF4,0x8F,0xBF,0xBF}}
};
}
extern "C" void NativeEntry() {
    Check(v8::base::gtos::CurrentProcessInfo().maximum_parallel_threads == 1, 1);
    GtosVmReserveResult result = {};
    GtosVmReserveRequest reserve = {1,4096,4096,0x80000000U,(unsigned)&result};
    Check(Call(GTOS_SYS_VM_RESERVE, &reserve, sizeof(reserve)) == 0, 2);
    Check(result.version == 1 && result.handle && result.base == 0x80000000U
        && result.length == 4096 && result.page_size == 4096, 3);
    GtosVmRangeRequest commit = {1,result.handle,0,4096,GTOS_VM_READ_WRITE};
    Check(Call(GTOS_SYS_VM_SET_PERMISSIONS, &commit, sizeof(commit)) == 0, 4);
    Check(*(volatile unsigned*)result.base == 0, 5);
    *(volatile unsigned*)result.base = 0xC011CA11U;
    Check(EmptyHeap(), 6);
    Check(sizeof(Date) == 36 && sizeof(time_t) == 8, 7);
    native_wide_record.region = result.handle;
    native_wide_record.base = result.base;
    native_wide_record.length = result.length;
    native_wide_record.pointer = (unsigned)&Date;
    native_wide_record.objectBytes = sizeof(Date);
    native_wide_record.heapEmpty = 1;
    native_wide_record.stage = 1;
    unsigned mode = native_wide_record.mode;
    mbstate_t state = {};
    wchar_t wide[8] = {};
    char output[32] = {};
    const char* src = "A";
    if (mode == 1) { Decode((wchar_t*)0xBFFFCFF0U, &src, 2, 2, &state); Exit(98); }
    if (mode == 2) { Read(wide, (const char*)0xBFFFCFF0U, 1, &state); Exit(98); }
    if (mode == 3) { Read(wide, "A", 1, (mbstate_t*)0xBFFFCFF0U); Exit(98); }
    if (mode == 4) {
        for (;;) {
            state = {}; src = "A\xE2\x82\xAC";
            Decode(wide, &src, 5, 8, &state, false);
            native_wide_record.cycles = native_wide_record.cycles + 1;
        }
    }
    unsigned splits = 0, capacities = 0;
    for (const Valid& item : Cases) {
        char text[5] = {};
        for (unsigned i = 0; i < item.size; ++i) text[i] = (char)item.bytes[i];
        state = {};
        Check(Write(output, (wchar_t)item.scalar, &state) == item.size && Empty(state), 100);
        for (unsigned i = 0; i < item.size; ++i) Check((unsigned char)output[i] == item.bytes[i], 101);
        state = {}; wide[0] = 0x55;
        Check(Read(wide, text, item.size, &state) == (item.scalar ? item.size : 0)
            && (unsigned)wide[0] == item.scalar && Empty(state), 102);
        Check(Legacy(wide, text, item.size) == (item.scalar ? (int)item.size : 0)
            && (unsigned)wide[0] == item.scalar, 103);
        for (unsigned cut = 1; cut < item.size; ++cut) {
            state = {}; wide[0] = 0x55;
            Check(Read(wide, text, cut, &state) == (size_t)-2 && wide[0] == 0x55
                && state.__field2 == cut, 110);
            Check(Read(wide, text + cut, item.size - cut, &state) == item.size - cut
                && (unsigned)wide[0] == item.scalar && Empty(state), 111);
            state = {};
            Check(Width(text, cut, &state) == (size_t)-2, 112);
            Check(Width(text + cut, item.size - cut, &state) == item.size - cut && Empty(state), 113);
            errno = 0;
            Check(Legacy(wide, text, cut) == -1 && errno == EILSEQ, 114);
            ++splits;
        }
        wchar_t input[] = {(wchar_t)item.scalar, 0};
        for (unsigned capacity = 0; capacity <= item.size + 1; ++capacity) {
            unsigned char guarded[16];
            for (unsigned char& byte : guarded) byte = 0xA5;
            const wchar_t* from = input; state = {};
            size_t count = Encode((char*)guarded + 4, &from, 2, capacity, &state);
            unsigned need = item.scalar ? item.size : 0;
            Check(count == (capacity >= item.size ? need : 0), 120);
            Check(from == (capacity >= need + 1 && capacity >= item.size ? nullptr
                : capacity >= item.size ? input + 1 : input), 121);
            unsigned stored = capacity >= item.size ? item.size : 0;
            if (item.scalar && capacity > item.size) { Check(guarded[4 + item.size] == 0, 122); ++stored; }
            for (unsigned i = 0; i < (capacity >= item.size ? item.size : 0); ++i)
                Check(guarded[4 + i] == item.bytes[i], 123);
            for (unsigned i = 0; i < 4; ++i) Check(guarded[i] == 0xA5, 124);
            for (unsigned i = 4 + stored; i < sizeof(guarded); ++i) Check(guarded[i] == 0xA5, 125);
            Check(Empty(state), 126); ++capacities;
        }
        src = text; state = {};
        Check(Decode(wide, &src, item.size + 1, 8, &state, false) == (item.scalar ? 1U : 0U)
            && src == nullptr && (unsigned)wide[0] == item.scalar && Empty(state), 130);
        src = text; state = {};
        Check(Decode(nullptr, &src, item.size + 1, 0, &state) == (item.scalar ? 1U : 0U)
            && src == text && Empty(state), 131);
        native_wide_record.masks = native_wide_record.masks + 1;
    }
    const unsigned char bad[][4] = {{0xC0,0xAF},{0xC1,0xBF},{0xE0,0x9F,0x80},
        {0xED,0xA0,0x80},{0xED,0xBF,0xBF},{0xF0,0x8F,0xBF,0xBF},
        {0xF4,0x90,0x80,0x80},{0xF5,0x80,0x80,0x80},{0x80},{0xE2,0}};
    const unsigned sizes[] = {2,2,3,3,3,4,4,4,1,2};
    for (unsigned n = 0; n < 10; ++n) {
        state = {}; wide[0] = 0x55; errno = 0;
        Check(Read(wide, (const char*)bad[n], sizes[n], &state) == (size_t)-1
            && errno == EILSEQ && wide[0] == 0x55, 200);
        char prefixed[6] = {'A'};
        for (unsigned i = 0; i < sizes[n]; ++i) prefixed[i + 1] = (char)bad[n][i];
        src = prefixed; state = {}; errno = 0;
        Check(Decode(wide, &src, sizes[n] + 1, 8, &state) == (size_t)-1
            && errno == EILSEQ && src == prefixed + 1 && wide[0] == L'A', 201);
        native_wide_record.rejected = native_wide_record.rejected + 1;
    }
    const unsigned invalid[] = {0xD800,0xDFFF,0x110000};
    for (unsigned value : invalid) {
        state = {}; output[0] = 'x'; errno = 0;
        Check(Write(output, (wchar_t)value, &state) == (size_t)-1 && errno == EILSEQ && output[0] == 'x', 202);
        native_wide_record.rejected = native_wide_record.rejected + 1;
    }
    const char split[] = "A\xE2\x82\xAC";
    src = split; state = {};
    Check(Decode(wide, &src, 2, 8, &state) == 1 && wide[0] == L'A'
        && src == split + 2 && state.__field2 == 1, 300);
    Check(Decode(wide + 1, &src, 3, 7, &state) == 1 && wide[1] == 0x20AC
        && src == nullptr && Empty(state), 301);
    state = {};
    Check(Read(wide, split + 1, 1, &state) == (size_t)-2, 302);
    errno = 0;
    Check(Read(wide, nullptr, 0, &state) == (size_t)-1 && errno == EILSEQ, 303);
    state = {}; src = (const char*)0xBFFFCFF0U;
    Check(Decode(wide, &src, 1, 0, &state, false) == 0 && src == (const char*)0xBFFFCFF0U, 304);
    const wchar_t* from = (const wchar_t*)0xBFFFCFF0U;
    Check(Encode(output, &from, 1, 0, &state) == 0 && from == (const wchar_t*)0xBFFFCFF0U, 305);
    Check(Read(wide, (const char*)0xBFFFCFF0U, 0, &state) == (size_t)-2, 306);
    Check(Width((const char*)0xBFFFCFF0U, 0, &state) == (size_t)-2, 307);
    state = {0,5,2}; errno = 0;
    Check(Read(wide, "A", 1, &state) == (size_t)-1 && errno == EINVAL, 308);
    char date[32];
    Check(std::__locale::__strftime(date, sizeof(date), "%F", &Date, nullptr) == 10, 309);
    src = date; state = {};
    Check(Decode(wide, &src, 11, 8, &state, false) == 8 && src == date + 8, 310);
    wchar_t tail[4];
    Check(Decode(tail, &src, 3, 4, &state, false) == 2 && src == nullptr
        && tail[0] == L'2' && tail[1] == L'9' && tail[2] == 0, 311);
    char* edge = (char*)(result.base + 4096 - 4);
    state = {};
    Check(Write(edge, 0x1F600, &state) == 4, 312);
    state = {};
    Check(Read(wide, edge, 4, &state) == 4 && wide[0] == 0x1F600, 313);
    Check(Read(wide, "\xC2", 1, nullptr) == (size_t)-2, 314);
    Check(Read(wide, "\xA2", 1, nullptr) == 1 && wide[0] == 0xA2, 315);
    Check(Width("A", 1, nullptr) == 1, 316);
    src = "A";
    Check(Decode(wide, &src, 2, 8, nullptr, false) == 1 && src == nullptr, 317);
    const wchar_t ascii[] = {L'A',0}; from = ascii;
    Check(Encode(output, &from, 2, sizeof(output), nullptr) == 1 && from == nullptr
        && output[0] == 'A' && output[1] == 0, 318);
    state = {};
    Check(Write(nullptr, 0xD800, &state) == 1 && Empty(state), 319);
    Check(Legacy(nullptr, nullptr, 4) == 0, 320);
    Check(EmptyHeap() && splits == 20 && capacities == 56 && native_wide_record.masks == 12
        && native_wide_record.rejected == 13 && native_wide_record.calls == 259, 400);
    native_wide_record.stage = 2;
    Exit(0);
}
