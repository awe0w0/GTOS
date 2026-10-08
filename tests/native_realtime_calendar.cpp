#include <process/native_realtime.h>
extern "C" int printf(const char*, ...);
using namespace gtos::process;
struct Vector { NativeRtcSnapshot rtc; uint64_t expected; };
#include "calendar-vectors.inc"
static unsigned checks;
static bool Check(bool value, unsigned line) {
    ++checks;
    if (!value) printf("REALTIME CHECK FAIL line=%u\n", line);
    return value;
}
#define CHECK(value) do { if (!Check((value), __LINE__)) return 1; } while (0)
static GtosClockReadResult Clock() {
    const GtosClockReadResult value = {1,1,1,1,7,10000,0,0,1193182,11931};
    return value;
}
static void Sentinel(void* data, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) ((uint8_t*)data)[i] = 0xA5;
}
static bool Unchanged(const void* data, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) if (((const uint8_t*)data)[i] != 0xA5) return false;
    return true;
}
int main() {
    for (unsigned i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
        const Vector& vector = vectors[i];
        uint64_t epoch = 0xA5A5A5A5A5A5A5A5ULL;
        CHECK(NativeRealtimeClock::Decode(vector.rtc, epoch) && epoch == vector.expected);
        NativeRealtimeClock realtime;
        GtosClockReadResult clock = Clock();
        CHECK(realtime.Initialize(vector.rtc, clock));
        CHECK(!realtime.Initialize(vector.rtc, clock));
        clock.microseconds = 9999; clock.delivered_ticks = 1;
        GtosRealtimeReadResult result;
        CHECK(realtime.Read(clock, result) == 0);
        CHECK(result.version == 1 && result.unit == 1 && result.source == 1
            && result.capabilities == 15 && result.resolution_us == 10000
            && result.anchor_uncertainty_us == 1000000);
        CHECK(result.microseconds == vector.expected + 9999
            && result.monotonic_microseconds == 9999 && result.delivered_ticks == 1);
    }
    const NativeRtcSnapshot valid = {0,0,0,1,1,0,0x20,2,0x26,0x80};
    const uint8_t bad[17][2] = {{0,0x6A},{0,0x60},{1,0x60},{2,0x24},{2,0x80},
        {3,0},{3,0x32},{4,0},{4,0x13},{5,0xA0},{6,0x18},{6,0xA0},
        {7,0x82},{7,3},{8,0xA6},{8,0x66},{9,0}};
    for (unsigned i = 0; i < 17; ++i) {
        NativeRtcSnapshot rtc = valid; ((uint8_t*)&rtc)[bad[i][0]] = bad[i][1];
        uint64_t epoch = 0xA5A5A5A5A5A5A5A5ULL;
        CHECK(!NativeRealtimeClock::Decode(rtc, epoch) && epoch == 0xA5A5A5A5A5A5A5A5ULL);
        NativeRealtimeClock realtime; GtosClockReadResult clock = Clock();
        CHECK(!realtime.Initialize(rtc, clock));
        GtosRealtimeReadResult result; Sentinel(&result, sizeof(result));
        CHECK(realtime.Read(clock, result) == GTOS_REALTIME_ERR_UNSUPPORTED && Unchanged(&result, sizeof(result)));
    }
    NativeRtcSnapshot rtc = valid; rtc.month = 2; rtc.day = 0x29; rtc.century = 0x21;
    uint64_t epoch = 1;
    CHECK(!NativeRealtimeClock::Decode(rtc, epoch) && epoch == 1); // Gregorian 2100 is not leap.
    rtc.year = 0x69; rtc.century = 0x19; rtc.month = 1; rtc.day = 1;
    CHECK(!NativeRealtimeClock::Decode(rtc, epoch) && epoch == 1);
    GtosClockReadResult clock = Clock(); NativeRealtimeClock realtime;
    clock.microseconds = 20000; clock.delivered_ticks = 2;
    CHECK(realtime.Initialize(valid, clock));
    GtosRealtimeReadResult result; Sentinel(&result, sizeof(result));
    clock.microseconds = 19999;
    CHECK(realtime.Read(clock, result) == GTOS_REALTIME_ERR_BAD_STATE && Unchanged(&result, sizeof(result)));
    clock.microseconds = 20000; clock.source = 99;
    CHECK(realtime.Read(clock, result) == GTOS_REALTIME_ERR_BAD_STATE && Unchanged(&result, sizeof(result)));
    clock = Clock(); clock.microseconds = ~(uint64_t)0;
    CHECK(realtime.Read(clock, result) == GTOS_REALTIME_ERR_OVERFLOW && Unchanged(&result, sizeof(result)));
    printf("NATIVE REALTIME CALENDAR PASS vectors=%u checks=%u\n", (unsigned)(sizeof(vectors)/sizeof(vectors[0])), checks);
    return 0;
}
