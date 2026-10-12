#ifndef NATIVE_TIME_FORMAT_RECORD_H
#define NATIVE_TIME_FORMAT_RECORD_H
struct NativeTimeFormatRecord {
    unsigned version, mode, stage, checks, error, calls, masks, rejected;
    unsigned cycles, pointer, objectBytes, heapEmpty, region, base, length, reserved;
};
static_assert(sizeof(NativeTimeFormatRecord) == 64, "TimeFormat record ABI");
#endif
