#ifndef GTOS_NATIVE_DIAGNOSTIC_RECORD_H
#define GTOS_NATIVE_DIAGNOSTIC_RECORD_H
struct NativeDiagRecord {
    unsigned version, mode, stage, checks, error, formattedBytes, textBytes, expectedBytes;
    unsigned progress, region, base, length, reserved[4];
};
static_assert(sizeof(NativeDiagRecord) == 64, "Diagnostic record size");
#endif
