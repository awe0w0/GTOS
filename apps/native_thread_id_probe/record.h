#ifndef GTOS_NATIVE_TLS_PROBE_RECORD_H
#define GTOS_NATIVE_TLS_PROBE_RECORD_H

#define GTOS_TLS_PROBE_RECORD_VA 0x40030000U
#define GTOS_TLS_PROBE_RECORD_BYTES 512U
#define GTOS_TLS_PROBE_GUARD_VA 0xBFFFCFFCU
#define GTOS_TLS_PROBE_MAX_OBJECTS 8U
#define GTOS_TLS_PROBE_PEER_NONCE 0x37U
#define GTOS_TLS_PROBE_VICTIM_NONCE 0xA9U
#define GTOS_TLS_PROBE_DIRECT_EXIT 0U

// This is acceptance data. The compiler descriptor ABI is qualified separately.
struct NativeTlsProbeObject {
    unsigned control_va, size, alignment, template_va;
    unsigned address, pattern_tag, byte_count, flags;
};
struct NativeTlsProbeRecord {
    unsigned version, bytes, kind, mode;
    unsigned stage, checks, error, nonce;
    unsigned try_before, current, try_after, repeated;
    unsigned heap_base, heap_handle, heap_length, heap_resident_pages;
    unsigned object_count, initialized_count, failed_slot, unpublished_address;
    unsigned finalize_called, finalize_completed, heap_empty, cache_reset_count;
    unsigned yield_checks, spin_progress, pattern_checks, reload_serial;
    unsigned last_request_control, first_handle, final_heap_handle, reserved;
    NativeTlsProbeObject objects[GTOS_TLS_PROBE_MAX_OBJECTS];
    unsigned reserved_tail[32];
};
static_assert(sizeof(unsigned) == 4, "IA32 record words");
static_assert(sizeof(NativeTlsProbeObject) == 32, "TLS object observation bytes");
static_assert(sizeof(NativeTlsProbeRecord) == GTOS_TLS_PROBE_RECORD_BYTES, "TLS probe record bytes");
static_assert(__builtin_offsetof(NativeTlsProbeRecord, objects) == 128, "TLS object observations offset");
static_assert(__builtin_offsetof(NativeTlsProbeRecord, reserved_tail) == 384, "TLS reserved tail offset");
#endif