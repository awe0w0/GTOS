#ifndef GTOS_NATIVE_SYNC_RECORD_H
#define GTOS_NATIVE_SYNC_RECORD_H
struct NativeSyncRecord {
    unsigned version, mode, stage, checks, error, rawChecks, stdChecks, waiterChecks;
    unsigned pending, region, base, length, mutexAddress, conditionAddress, clockErrors, reserved;
};
static_assert(sizeof(NativeSyncRecord) == 64, "Synchronization record ABI");
#endif
