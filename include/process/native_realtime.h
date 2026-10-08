#ifndef __GTOS__PROCESS__NATIVE_REALTIME_H
#define __GTOS__PROCESS__NATIVE_REALTIME_H
#include <common/types.h>
#include <process/clock_abi.h>
#include <process/realtime_abi.h>
namespace gtos {
    struct NativeRealtimeFixture;
    namespace process {
        struct NativeRtcSnapshot {
            uint8_t second, minute, hour, day, month, year, century;
            uint8_t format, divider, valid;
        };
        // BSP boot context with IF clear; never called from a user syscall.
        bool ReadNativeRtc(NativeRtcSnapshot& result);
        class NativeRealtimeClock {
            friend struct ::gtos::NativeRealtimeFixture;
            uint64_t epochMicroseconds, monotonicAnchor;
            bool ready;
            static bool Number(uint8_t byte, bool binary, uint32_t& value) {
                if (!binary && ((byte & 15) > 9 || (byte >> 4) > 9)) return false;
                value = binary ? byte : (byte >> 4) * 10U + (byte & 15);
                return true;
            }
            static bool Leap(uint32_t year) {
                return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
            }
            static uint32_t LeapsBefore(uint32_t year) {
                --year;
                return year / 4 - year / 100 + year / 400;
            }
            static bool ClockValid(const GtosClockReadResult& clock) {
                return clock.version == GTOS_CLOCK_ABI_VERSION
                    && clock.clock_id == GTOS_CLOCK_ID_MONOTONIC
                    && clock.unit == GTOS_CLOCK_UNIT_MICROSECONDS
                    && clock.source == GTOS_CLOCK_SOURCE_PIT_DELIVERED_IRQ
                    && clock.capabilities == GTOS_CLOCK_REQUIRED_CAPABILITIES
                    && clock.resolution_us == GTOS_CLOCK_RESOLUTION_US
                    && clock.pit_input_hz == GTOS_CLOCK_PIT_INPUT_HZ
                    && clock.pit_divisor == GTOS_CLOCK_PIT_DIVISOR;
            }
            NativeRealtimeClock(const NativeRealtimeClock&);
            NativeRealtimeClock& operator=(const NativeRealtimeClock&);
        public:
            NativeRealtimeClock() : epochMicroseconds(0), monotonicAnchor(0), ready(false) {}
            static bool Decode(const NativeRtcSnapshot& rtc, uint64_t& epoch) {
                if (!(rtc.valid & 0x80) || (rtc.divider & 0xF0) != 0x20
                    || (rtc.format & 0x81)) return false;
                const bool binary = (rtc.format & 4) != 0;
                uint32_t second, minute, hour, day, month, year, century;
                if (!Number(rtc.second, binary, second) || !Number(rtc.minute, binary, minute)
                    || !Number(rtc.hour & 0x7F, binary, hour) || !Number(rtc.day, binary, day)
                    || !Number(rtc.month, binary, month) || !Number(rtc.year, binary, year)
                    || !Number(rtc.century, binary, century)) return false;
                if (second > 59 || minute > 59 || month < 1 || month > 12
                    || year > 99 || century < 19 || century > 99) return false;
                if (rtc.format & 2) {
                    if ((rtc.hour & 0x80) || hour > 23) return false;
                } else {
                    if (hour < 1 || hour > 12) return false;
                    hour %= 12;
                    if (rtc.hour & 0x80) hour += 12;
                }
                year += century * 100;
                if (year < 1970 || year > 9999) return false;
                const uint8_t lengths[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
                const uint32_t limit = lengths[month - 1] + (month == 2 && Leap(year));
                if (day < 1 || day > limit) return false;
                uint32_t days = (year - 1970) * 365 + LeapsBefore(year) - LeapsBefore(1970);
                for (uint32_t m = 1; m < month; ++m)
                    days += lengths[m - 1] + (m == 2 && Leap(year));
                days += day - 1;
                const uint64_t seconds = (uint64_t)days * 86400U + hour * 3600U + minute * 60U + second;
                epoch = seconds * 1000000U;
                return true;
            }
            bool Initialize(const NativeRtcSnapshot& rtc, const GtosClockReadResult& clock) {
                if (ready || !ClockValid(clock)) return false;
                uint64_t epoch;
                if (!Decode(rtc, epoch)) return false;
                epochMicroseconds = epoch; monotonicAnchor = clock.microseconds; ready = true;
                return true;
            }
            int Read(const GtosClockReadResult& clock, GtosRealtimeReadResult& result) const {
                if (!ready) return GTOS_REALTIME_ERR_UNSUPPORTED;
                if (!ClockValid(clock) || clock.microseconds < monotonicAnchor)
                    return GTOS_REALTIME_ERR_BAD_STATE;
                const uint64_t elapsed = clock.microseconds - monotonicAnchor;
                if (epochMicroseconds > ~(uint64_t)0 - elapsed) return GTOS_REALTIME_ERR_OVERFLOW;
                const GtosRealtimeReadResult value = {
                    GTOS_REALTIME_ABI_VERSION, GTOS_REALTIME_UNIT_MICROSECONDS,
                    GTOS_REALTIME_SOURCE_CMOS_PIT, GTOS_REALTIME_REQUIRED_CAPABILITIES,
                    GTOS_REALTIME_RESOLUTION_US, GTOS_REALTIME_ANCHOR_UNCERTAINTY_US,
                    epochMicroseconds + elapsed, clock.microseconds, clock.delivered_ticks};
                result = value;
                return 0;
            }
        };
    }
}
#endif
