#ifndef NATIVE_CHRONO_RECORD_H
#define NATIVE_CHRONO_RECORD_H
struct NativeChronoRecord {
    unsigned version, mode, stage, checks, error, calls, mono, wall;
    unsigned cycles, pointer, objectBytes, heapEmpty, region, base, length, pending;
};
static_assert(sizeof(NativeChronoRecord) == 64, "Chrono record ABI");
#endif
