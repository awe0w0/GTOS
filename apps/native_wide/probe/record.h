#ifndef NATIVE_WIDE_RECORD_H
#define NATIVE_WIDE_RECORD_H
struct NativeWideRecord {
    unsigned version, mode, stage, checks, error, calls, masks, rejected;
    unsigned cycles, pointer, objectBytes, heapEmpty, region, base, length, reserved;
};
static_assert(sizeof(NativeWideRecord) == 64, "Wide record ABI");
#endif
