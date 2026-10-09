#ifndef GTOS_NATIVE_STRING_RECORD_H
#define GTOS_NATIVE_STRING_RECORD_H
struct StringRecord {
    unsigned version, mode, stage, error, checks, cases, base, handle;
    unsigned errno_va, errno_value, heap_base, heap_bytes;
    unsigned suites, repeats, checksum, reserved;
    unsigned calls[13];
};
#define GTOS_STRING_RECORD_VA 0x40040000U
static_assert(sizeof(StringRecord) == 116, "Serialized byte/string record");
#endif
