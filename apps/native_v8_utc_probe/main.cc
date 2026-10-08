#include "record.h"
#include "src/base/platform/gtos-clock-bridge.h"
#include "src/base/platform/gtos-native-services.h"
#include "src/base/platform/gtos-native-api/vm_abi.h"
#include "src/base/platform/time.h"
extern "C" {
__attribute__((section(".data.utc_record"), used))
volatile UtcRecord native_utc_record = {1, GTOS_UTC_MODE, 0, 0, 0, 0, 0, 0, 0, 9, {}, {}, 0, 0};
}
static int Call(unsigned operation, const void* request, unsigned bytes) {
    asm volatile("int $0x80" : "+a"(operation)
        : "b"(static_cast<unsigned>(reinterpret_cast<uintptr_t>(request))), "c"(bytes) : "memory", "cc");
    return static_cast<int>(operation);
}
static void Fail(unsigned error) __attribute__((noreturn));
static void Fail(unsigned error) {
    native_utc_record.error = error;
    Call(GTOS_SYS_EXIT, reinterpret_cast<void*>(0x4B000000U | error), 0);
    __builtin_trap();
}
static void Require(bool condition, unsigned error) {
    if (!condition) Fail(error);
    native_utc_record.checks = native_utc_record.checks + 1;
}
#if GTOS_UTC_MODE != 3
static void Reject(const GtosRealtimeReadResult* value) {
    Require(!gtos_v8_realtime_validate(value), 1);
    native_utc_record.rejected = native_utc_record.rejected + 1;
}
#endif
static void Keep() {
    GtosVmReserveResult result = {};
    GtosVmReserveRequest request = {1, 8192, 4096, 0,
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(&result))};
    Require(Call(GTOS_SYS_VM_RESERVE, &request, sizeof(request)) == 0, 2);
    Require(result.version == 1 && result.base == 0x80000000U && result.length == 8192
        && result.page_size == 4096 && result.handle, 3);
    GtosVmRangeRequest range = {1, result.handle, 0, 8192, GTOS_VM_READ_WRITE};
    Require(Call(GTOS_SYS_VM_SET_PERMISSIONS, &range, sizeof(range)) == 0, 4);
    native_utc_record.base = result.base; native_utc_record.handle = result.handle;
    native_utc_record.keep_pages = 2;
    volatile unsigned char* pages = reinterpret_cast<volatile unsigned char*>(result.base);
    for (unsigned i = 0; i < 8192; ++i) pages[i] = static_cast<unsigned char>(i ^ 0x5DU);
}
#if GTOS_UTC_MODE != 3
static void Validator() {
    GtosRealtimeReadResult raw = {};
    GtosRealtimeReadRequest request = {1, 0,
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(&raw)), sizeof(raw)};
    Require(Call(GTOS_SYS_REALTIME_READ, &request, sizeof(request)) == 0, 5);
    Require(gtos_v8_realtime_validate(&raw), 6);
    volatile unsigned char* out = reinterpret_cast<volatile unsigned char*>(&native_utc_record.raw);
    const unsigned char* in = reinterpret_cast<const unsigned char*>(&raw);
    for (unsigned i = 0; i < sizeof(raw); ++i) out[i] = in[i];
    Reject(nullptr);
    for (unsigned index = 0; index < 6; ++index) for (unsigned change = 0; change < 3; ++change) {
        GtosRealtimeReadResult value = raw;
        unsigned* word = nullptr;
        switch (index) {
            case 0: word = &value.version; break;
            case 1: word = &value.unit; break;
            case 2: word = &value.source; break;
            case 3: word = &value.capabilities; break;
            case 4: word = &value.resolution_us; break;
            case 5: word = &value.anchor_uncertainty_us; break;
            default: Fail(14);
        }
        *word = change == 0 ? 0U : (change == 1 ? *word ^ 0x80000000U : 0xffffffffU);
        Reject(&value);
    }
    const unsigned long long invalid[] = {0x7fffffffffffffffULL, 0x8000000000000000ULL, 0xffffffffffffffffULL};
    for (unsigned long long micros : invalid) {
        GtosRealtimeReadResult value = raw; value.microseconds = micros; Reject(&value);
    }
    const unsigned long long valid[] = {0, 1, 0xffffffffULL, 0x100000000ULL, 0x7ffffffffffffffeULL};
    for (unsigned long long micros : valid) {
        GtosRealtimeReadResult value = raw; value.microseconds = micros;
        Require(gtos_v8_realtime_validate(&value), 7);
    }
}
#endif
static void Sample(unsigned kind) {
    const unsigned index = native_utc_record.sample_count;
    Require(index < 8, 8);
    native_utc_record.pending_kind = kind;
    unsigned long long value; double js = 0;
    if (kind == 0) {
        value = static_cast<unsigned long long>(v8::base::TimeTicks::Now().ToInternalValue());
    } else {
        const v8::base::Time now = kind == 1 ? v8::base::Time::Now() : v8::base::Time::NowFromSystemTime();
        // ToInternalValue is used only to serialize the actual method result.
        value = static_cast<unsigned long long>(now.ToInternalValue());
        js = now.ToJsTime();
        Require(!now.IsMax(), 9);
    }
    native_utc_record.samples[index].kind = kind;
    native_utc_record.samples[index].value = value;
    native_utc_record.samples[index].js_ms = js;
    native_utc_record.sample_count = index + 1;
    native_utc_record.pending_kind = 9;
}
extern "C" void NativeEntry() {
    Keep();
    Require(!v8::base::TimeTicks::IsHighResolution() && !v8::base::ThreadTicks::IsSupported(), 10);
    native_utc_record.stage = 1;
#if GTOS_UTC_MODE == 3
    Sample(0);
    // A real unavailable RTC must take the unchanged explicit fatal path.
    Sample(1);
    Fail(11);
#else
    Validator();
    for (unsigned round = 0; round < 2; ++round) {
        Sample(0); Sample(1); Sample(2); Sample(0);
        if (round == 0) {
            const unsigned long long begin = native_utc_record.samples[0].value;
            for (;;) {
                const unsigned long long now = static_cast<unsigned long long>(
                    v8::base::TimeTicks::Now().ToInternalValue());
                Require(now >= begin && now - begin <= 1000000ULL, 12);
                if (now - begin >= 30000ULL) break;
                v8::base::gtos::Yield();
            }
        }
    }
    volatile unsigned char* pages = reinterpret_cast<volatile unsigned char*>(native_utc_record.base);
    for (unsigned i = 0; i < 8192; ++i) Require(pages[i] == static_cast<unsigned char>(i ^ 0x5DU), 13);
    native_utc_record.sentinel_checks = 8192;
    native_utc_record.stage = 2;
#if GTOS_UTC_MODE == 1
    *reinterpret_cast<volatile unsigned*>(0xBFFFCFFCU) = 0x55544331U;
#elif GTOS_UTC_MODE == 2
    for (;;) v8::base::gtos::Yield();
#else
    Call(GTOS_SYS_EXIT, nullptr, 0);
#endif
#if GTOS_UTC_MODE != 2
    __builtin_trap();
#endif
#endif
}
