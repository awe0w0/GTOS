#include <process/native_realtime.h>
#include <hardwarecommunication/port.h>
#include <memory/criticalsection.h>
using namespace gtos;
using namespace gtos::process;
using namespace gtos::hardwarecommunication;
namespace {
    uint8_t Register(Port8Bit& index, Port8Bit& data, uint8_t number) {
        // Select with NMI enabled. Never write CMOS data/control registers.
        index.Port8Bit::Write(number);
        return data.Port8Bit::Read();
    }
    bool Sample(Port8Bit& index, Port8Bit& data, NativeRtcSnapshot& result) {
        const uint8_t divider = Register(index, data, 0x0A);
        if (divider & 0x80) return false;
        const NativeRtcSnapshot value = {
            Register(index, data, 0), Register(index, data, 2), Register(index, data, 4),
            Register(index, data, 7), Register(index, data, 8), Register(index, data, 9),
            Register(index, data, 0x32), Register(index, data, 0x0B),
            divider, Register(index, data, 0x0D)};
        if (Register(index, data, 0x0A) != divider
            || Register(index, data, 0x0B) != value.format) return false;
        result = value;
        return true;
    }
    bool Same(const NativeRtcSnapshot& a, const NativeRtcSnapshot& b) {
        return a.second == b.second && a.minute == b.minute && a.hour == b.hour
            && a.day == b.day && a.month == b.month && a.year == b.year
            && a.century == b.century && a.format == b.format
            && a.divider == b.divider && a.valid == b.valid;
    }
}
bool gtos::process::ReadNativeRtc(NativeRtcSnapshot& result) {
    memory::InterruptGuard guard;
    Port8Bit index(0x70), data(0x71);
    for (uint32_t attempt = 0; attempt < 1024; ++attempt) {
        NativeRtcSnapshot first, second;
        uint64_t epoch;
        if (Sample(index, data, first) && Sample(index, data, second)
            && Same(first, second) && NativeRealtimeClock::Decode(second, epoch)) {
            result = second;
            return true;
        }
    }
    return false;
}
