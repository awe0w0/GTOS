#include "record.h"
#include "heap.h"
#include "src/base/platform/gtos-native-services.h"
#include "src/base/platform/gtos-native-api/vm_abi.h"
#include <string.h>
#include <wchar.h>
#include <errno.h>
#include <stdint.h>
static_assert(sizeof(size_t) == 4 && sizeof(wchar_t) == 4, "Actual IA32 LLVM SDK");
extern "C" {
__attribute__((section(".data.string_record"), used))
volatile StringRecord native_string_record = {1, GTOS_STRING_MODE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {}};
}
static int Call(unsigned operation, uintptr_t argument, unsigned bytes) {
    asm volatile("int $0x80" : "+a"(operation) : "b"(argument), "c"(bytes) : "memory", "cc");
    return static_cast<int>(operation);
}
static void Require(bool pass, unsigned code) {
    if (!pass) {
        native_string_record.error = code;
        Call(GTOS_SYS_EXIT, 0x52000000U | code, 0);
        __builtin_trap();
    }
    native_string_record.checks = native_string_record.checks + 1;
}
static void Seen(unsigned index) {
    native_string_record.calls[index] = native_string_record.calls[index] + 1;
    native_string_record.suites = native_string_record.suites | (1U << index);
}
static unsigned char Pattern(unsigned i) { return static_cast<unsigned char>(i * 73U + 19U); }
static void Fill(unsigned char* p, unsigned n, unsigned seed) {
    volatile unsigned char* data = p;
    for (unsigned i = 0; i < n; ++i) data[i] = static_cast<unsigned char>(Pattern(i) ^ seed);
}
static int Sign(int value) { return value < 0 ? -1 : (value > 0 ? 1 : 0); }
static void CheckBytes(const unsigned char* p, unsigned n, unsigned seed, unsigned code) {
    const volatile unsigned char* data = p;
    for (unsigned i = 0; i < n; ++i) Require(data[i] == static_cast<unsigned char>(Pattern(i) ^ seed), code);
}
static void CopyCases() {
    unsigned char* source = reinterpret_cast<unsigned char*>(0x80001040U);
    unsigned char* target = reinterpret_cast<unsigned char*>(0x80002040U);
    const unsigned lengths[] = {0, 1, 2, 3, 4, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129, 255, 256, 257, 511, 512, 513, 1023, 1024};
    for (unsigned sa = 0; sa < 16; ++sa) for (unsigned da = 0; da < 16; ++da) for (unsigned n : lengths) {
        unsigned char* s = source + sa; unsigned char* d = target + da;
        Fill(s, n, 0x53); Fill(d - 16, n + 32, 0xC1);
        Seen(0); Require(memcpy(d, s, n) == d, 1);
        CheckBytes(d, n, 0x53, 2);
        CheckBytes(d - 16, 16, 0xC1, 3);
        for (unsigned i = 0; i < 16; ++i) Require(d[n + i] == static_cast<unsigned char>(Pattern(n + 16 + i) ^ 0xC1), 4);
        Seen(3); Require(memcmp(d, s, n) == 0, 5);
        Seen(2); Require(memset(d, 0x1A5, n) == d, 6);
        for (unsigned i = 0; i < n; ++i) Require(d[i] == 0xA5, 7);
        for (unsigned i = 0; i < 16; ++i) {
            Require(d[-16 + static_cast<int>(i)] == static_cast<unsigned char>(Pattern(i) ^ 0xC1), 8);
            Require(d[n + i] == static_cast<unsigned char>(Pattern(n + 16 + i) ^ 0xC1), 9);
        }
        native_string_record.cases = native_string_record.cases + 1;
    }
}
static void MoveCases() {
    unsigned char* area = reinterpret_cast<unsigned char*>(0x80003000U);
    const unsigned lengths[] = {0, 1, 2, 3, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129, 255, 256, 257, 511, 512, 513, 1024};
    const int shifts[] = {-33, -17, -16, -15, -7, -1, 0, 1, 7, 15, 16, 17, 33};
    for (unsigned alignment = 0; alignment < 16; ++alignment) for (unsigned n : lengths) for (int shift : shifts) {
        const unsigned from = 128 + alignment, to = static_cast<unsigned>(static_cast<int>(from) + shift);
        Fill(area, 1280, 0x31);
        Seen(1); Require(memmove(area + to, area + from, n) == area + to, 10);
        for (unsigned i = 0; i < 1280; ++i) {
            const unsigned original = i >= to && i - to < n ? from + i - to : i;
            Require(area[i] == static_cast<unsigned char>(Pattern(original) ^ 0x31), 11);
        }
        native_string_record.cases = native_string_record.cases + 1;
    }
}
static void CompareCases() {
    unsigned char* a = reinterpret_cast<unsigned char*>(0x80001000U);
    unsigned char* b = reinterpret_cast<unsigned char*>(0x80002000U);
    for (unsigned x = 0; x < 256; ++x) for (unsigned y = 0; y < 256; ++y) {
        a[0] = static_cast<unsigned char>(x); a[1] = 0;
        b[0] = static_cast<unsigned char>(y); b[1] = 0;
        const int expected = x < y ? -1 : (x > y ? 1 : 0);
        Seen(3); Require(Sign(memcmp(a, b, 1)) == expected, 12);
        Seen(6); Require(Sign(strcmp(reinterpret_cast<char*>(a), reinterpret_cast<char*>(b))) == expected, 13);
        Seen(7); Require(Sign(strncmp(reinterpret_cast<char*>(a), reinterpret_cast<char*>(b), 1)) == expected, 14);
        Seen(7); Require(strncmp(reinterpret_cast<char*>(a), reinterpret_cast<char*>(b), 0) == 0, 15);
        native_string_record.cases = native_string_record.cases + 1;
    }
    for (unsigned n = 0; n < 65; ++n) {
        Fill(a, n, 0); Fill(b, n, 0);
        for (unsigned at = 0; at < n; ++at) {
            const unsigned char old = b[at]; b[at] = static_cast<unsigned char>(old ^ 0x80);
            const int expected = a[at] < b[at] ? -1 : 1;
            Seen(3); Require(Sign(memcmp(a, b, n)) == expected, 16);
            b[at] = old;
        }
    }
}
static void SearchCases() {
    unsigned char* p = reinterpret_cast<unsigned char*>(0x80003000U);
    for (unsigned n = 0; n < 65; ++n) for (unsigned value = 0; value < 256; ++value) {
        for (unsigned i = 0; i < n; ++i) p[i] = static_cast<unsigned char>(i + 1);
        unsigned char* expected = value >= 1 && value <= n ? p + value - 1 : nullptr;
        Seen(4); Require(memchr(p, static_cast<int>(value + 256), n) == expected, 17);
        p[n] = 0;
        Seen(8); Require(strchr(reinterpret_cast<char*>(p), static_cast<int>(value + 256)) ==
            reinterpret_cast<char*>(value == 0 ? p + n : expected), 18);
        native_string_record.cases = native_string_record.cases + 1;
    }
}
static void StringCases() {
    char* a = reinterpret_cast<char*>(0x80001000U);
    char* b = reinterpret_cast<char*>(0x80002000U);
    for (unsigned align = 0; align < 16; ++align) for (unsigned n = 0; n < 257; ++n) {
        char* s = a + align; char* d = b + align;
        for (unsigned i = 0; i < n; ++i) s[i] = static_cast<char>((i * 19 + 1) % 255 + 1);
        s[n] = 0; d[n + 1] = 0x5A;
        Seen(5); Require(strlen(s) == n, 19);
        Seen(10); Require(strcpy(d, s) == d, 20);
        for (unsigned i = 0; i <= n; ++i) Require(d[i] == s[i], 21);
        Require(d[n + 1] == 0x5A, 22);
        native_string_record.cases = native_string_record.cases + 1;
    }
    // Independent simple matching model with repeated and high-byte inputs.
    const char* haystacks[] = {"", "a", "aaaaabaaaaa", "ababababacab", "GTOS Chromium native", "\x80\xFF\x80\xFE"};
    const char* needles[] = {"", "a", "aaab", "abac", "native", "absent", "\x80\xFE", "\xFF"};
    for (const char* h : haystacks) for (const char* needle : needles) {
        const volatile char* hv = h; const volatile char* nv = needle;
        unsigned hn = 0, nn = 0; while (hv[hn]) ++hn; while (nv[nn]) ++nn;
        const char* expected = nullptr;
        for (unsigned at = 0; at <= hn; ++at) {
            unsigned i = 0;
            while (i < nn && at + i < hn && static_cast<unsigned char>(hv[at + i]) == static_cast<unsigned char>(nv[i])) ++i;
            if (i == nn) { expected = h + at; break; }
        }
        Seen(9); Require(strstr(h, needle) == expected, 23);
        native_string_record.cases = native_string_record.cases + 1;
    }
}
static void WideCases() {
    wchar_t* p = reinterpret_cast<wchar_t*>(0x80003000U);
    const unsigned values[] = {1, 0x7F, 0x80, 0xFFFF, 0x1F600, 0x7FFFFFFF, 0x80000000U, 0xFFFFFFFFU};
    for (unsigned offset = 0; offset < 4; ++offset) for (unsigned n = 0; n < 65; ++n) {
        wchar_t* s = p + offset;
        for (unsigned i = 0; i < n; ++i) s[i] = static_cast<wchar_t>(values[i % 8]);
        s[n] = 0;
        Seen(12); Require(wcslen(s) == n, 24);
        for (unsigned j = 0; j < 9; ++j) {
            const wchar_t value = j == 8 ? 0 : static_cast<wchar_t>(values[j]);
            const wchar_t* expected = j < n && j < 8 ? s + j : nullptr;
            Seen(11); Require(wmemchr(s, value, n) == expected, 25);
        }
        native_string_record.cases = native_string_record.cases + 1;
    }
}
static void BoundaryCases() {
    unsigned char* begin = reinterpret_cast<unsigned char*>(0x80001000U);
    unsigned char* end = reinterpret_cast<unsigned char*>(0x80004000U);
    const unsigned lengths[] = {0, 1, 2, 3, 4, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129, 255, 256, 257, 511, 512, 513, 1023, 1024, 1025, 2047, 2048, 2049, 4095, 4096};
    for (unsigned n : lengths) {
        Fill(begin, n, 0xA7);
        Seen(0); Require(memcpy(end - n, begin, n) == end - n, 26);
        CheckBytes(end - n, n, 0xA7, 27);
        Seen(3); Require(memcmp(end - n, begin, n) == 0, 28);
        const volatile unsigned char* oracle = begin;
        unsigned first = 0; while (first < n && oracle[first] != 0xFF) ++first;
        Seen(4); Require(memchr(end - n, 0x1FF, n) == (first < n ? end - n + first : nullptr), 29);
        Seen(0); Require(memcpy(begin, end - n, n) == begin, 30);
        Seen(1); Require(memmove(end - n, begin, n) == end - n, 31);
        CheckBytes(end - n, n, 0xA7, 32);
        Seen(2); Require(memset(end - n, 0x3C, n) == end - n, 33);
        for (unsigned i = 0; i < n; ++i) Require(end[-static_cast<int>(n) + static_cast<int>(i)] == 0x3C, 34);
        native_string_record.cases = native_string_record.cases + 1;
    }
    for (unsigned n = 0; n < 257; ++n) {
        char* s = reinterpret_cast<char*>(end - n - 1);
        char* d = reinterpret_cast<char*>(begin);
        for (unsigned i = 0; i < n; ++i) s[i] = 'x';
        s[n] = 0;
        Seen(5); Require(strlen(s) == n, 35);
        Seen(8); Require(strchr(s, 0) == s + n, 36);
        Seen(10); Require(strcpy(d, s) == d, 37);
        Seen(6); Require(strcmp(d, s) == 0, 38);
        Seen(7); Require(strncmp(d, s, n + 1) == 0, 39);
        Seen(9); Require(strstr(s, "") == s, 40);
        native_string_record.cases = native_string_record.cases + 1;
    }
    for (unsigned n = 0; n < 65; ++n) {
        wchar_t* s = reinterpret_cast<wchar_t*>(end) - n - 1;
        for (unsigned i = 0; i < n; ++i) s[i] = static_cast<wchar_t>(0x1F600);
        s[n] = 0;
        Seen(12); Require(wcslen(s) == n, 41);
        Seen(11); Require(wmemchr(s, 0, n + 1) == s + n, 42);
        native_string_record.cases = native_string_record.cases + 1;
    }
}
extern "C" void NativeEntry() {
    GtosVmReserveResult result = {};
    GtosVmReserveRequest request = {1, 20480, 4096, 0, static_cast<unsigned>(reinterpret_cast<uintptr_t>(&result))};
    Require(Call(GTOS_SYS_VM_RESERVE, reinterpret_cast<uintptr_t>(&request), sizeof(request)) == 0, 43);
    Require(result.base == 0x80000000U && result.length == 20480 && result.handle && result.page_size == 4096, 44);
    GtosVmRangeRequest permissions = {1, result.handle, 4096, 12288, GTOS_VM_READ_WRITE};
    Require(Call(GTOS_SYS_VM_SET_PERMISSIONS, reinterpret_cast<uintptr_t>(&permissions), sizeof(permissions)) == 0, 45);
    native_string_record.base = result.base; native_string_record.handle = result.handle;
    Require(errno == 0, 46); errno = 0x5533;
    native_string_record.errno_va = static_cast<unsigned>(reinterpret_cast<uintptr_t>(&errno));
    native_string_record.stage = 1;
    CopyCases(); MoveCases(); CompareCases(); SearchCases(); StringCases(); WideCases(); BoundaryCases();
    Require(errno == 0x5533 && native_string_record.suites == 8191, 47);
    GtosVmRegionInfo heap = {};
    Require(gtos_native_heap_query(&heap) == 1 && heap.length == 65536, 48);
    native_string_record.heap_base = heap.base; native_string_record.heap_bytes = heap.length;
    native_string_record.errno_value = static_cast<unsigned>(errno);
    native_string_record.stage = 2;
    if (GTOS_STRING_MODE == 1) {
        native_string_record.stage = 3;
        Seen(3); volatile int fault = memcmp(reinterpret_cast<const void*>(0x80004000U), reinterpret_cast<const void*>(0x80001000U), 1);
        (void)fault;
    } else if (GTOS_STRING_MODE == 2) {
        native_string_record.stage = 3;
        for (;;) {
            Seen(3); Require(memcmp(reinterpret_cast<const void*>(0x80001000U), reinterpret_cast<const void*>(0x80001000U), 1024) == 0, 49);
            native_string_record.repeats = native_string_record.repeats + 1;
            Call(GTOS_SYS_YIELD, 0, 0);
        }
    } else if (GTOS_STRING_MODE == 3) {
        native_string_record.stage = 3;
        Seen(2); memset(reinterpret_cast<void*>(0x80004000U), 0xA5, 8);
    } else Call(GTOS_SYS_EXIT, 0, 0);
    native_string_record.error = 90; Call(GTOS_SYS_EXIT, 90, 0); __builtin_trap();
}
