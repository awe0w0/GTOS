#include "probe.h"
#include "thread_id_calls.h"
#include "tls_calls.h"
extern "C" {
volatile NativeTlsProbeRecord native_tls_record __attribute__((section(".data.tls_record"), used)) = {
    1, GTOS_TLS_PROBE_RECORD_BYTES, 1, GTOS_TLS_PROBE_MODE,
    0, 0, 0, GTOS_TLS_PROBE_VICTIM_NONCE,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, {}, {}
};
}
namespace {
void Require(bool condition, unsigned code) {
    if (!condition) tls_probe_fail(code);
    native_tls_record.checks = native_tls_record.checks + 1;
}
unsigned Pointer(const void* value) { return reinterpret_cast<unsigned>(value); }
unsigned Slot(unsigned role) {
    const unsigned sizes[5] = {4, 257, 64, 64, 65504};
    const unsigned alignments[5] = {4, 64, 16, 4096, 16};
    for (unsigned slot = 0; slot < gtos_emutls_spec_count; ++slot) {
        const auto& spec = gtos_emutls_specs[slot];
        if (spec.size == sizes[role] && spec.alignment == alignments[role]) return slot;
    }
    tls_probe_fail(1);
}
unsigned Control(unsigned slot) { return Pointer(__gtos_emutls_controls_start) + gtos_emutls_specs[slot].control_offset; }
void Observe(unsigned expected_live) {
    GtosEmutlsInfo info{};
    Require(gtos_emutls_query(&info) == 1 && info.version == 1 && info.count == 5 && info.live == expected_live && !info.finalized, 2);
    GtosVmRegionInfo heap{};
    Require(gtos_native_heap_query(&heap) == 1 && heap.version == 1 && heap.length == 65536 && heap.resident_pages == 16, 3);
    native_tls_record.heap_base = heap.base;
    native_tls_record.heap_handle = heap.handle;
    native_tls_record.heap_length = heap.length;
    native_tls_record.heap_resident_pages = heap.resident_pages;
    if (!native_tls_record.first_handle) native_tls_record.first_handle = heap.handle;
    native_tls_record.final_heap_handle = heap.handle;
    native_tls_record.object_count = 5;
    native_tls_record.initialized_count = expected_live;
    for (unsigned role = 0; role < 5; ++role) {
        const unsigned slot = Slot(role);
        const auto& spec = gtos_emutls_specs[slot];
        auto& object = native_tls_record.objects[role];
        object.control_va = Control(slot);
        object.size = spec.size;
        object.alignment = spec.alignment;
        object.template_va = Pointer(spec.initial_value);
        object.address = info.cached_address[slot];
        object.pattern_tag = role ? ((native_tls_record.nonce ^ (0x53U * role)) & 255U) : 1U;
        object.byte_count = info.cached_address[slot] ? spec.size : 0;
        object.flags = role | (slot << 8) | (info.cached_address[slot] ? 0x10000U : 0);
    }
}
void Hold(unsigned ticks) {
    const unsigned first = static_cast<unsigned>(tls_probe_call(GTOS_SYS_TICKS, 0, 0));
    do {
        tls_probe_call(GTOS_SYS_YIELD, 0, 0);
        native_tls_record.yield_checks = native_tls_record.yield_checks + 1;
        native_tls_record.spin_progress = native_tls_record.spin_progress + 1;
    } while (static_cast<unsigned>(tls_probe_call(GTOS_SYS_TICKS, 0, 0)) - first < ticks);
}
void Initial(unsigned char* bytes, unsigned length, unsigned role) {
    Require(Pointer(bytes) % gtos_emutls_specs[Slot(role)].alignment == 0, 4);
    for (unsigned i = 0; i < length; ++i) {
        unsigned expected = 0;
        if (role == 2 && i < 3) expected = i == 0 ? 0xA5U : (i == 1 ? 0x69U : 0x37U);
        if (role == 3 && i < 3) expected = i == 0 ? 0xC4U : (i == 1 ? 0x1BU : 0xA9U);
        Require(bytes[i] == expected, 5);
    }
}
void Fill(unsigned char* bytes, unsigned length, unsigned role) {
    for (unsigned i = 0; i < length; ++i) bytes[i] = static_cast<unsigned char>(i ^ (native_tls_record.nonce ^ (0x53U * role)));
}
void Pattern(unsigned char* bytes, unsigned length, unsigned role) {
    for (unsigned i = 0; i < length; ++i) Require(bytes[i] == static_cast<unsigned char>(i ^ (native_tls_record.nonce ^ (0x53U * role))), 6);
    native_tls_record.pattern_checks = native_tls_record.pattern_checks + length;
}
[[maybe_unused]] void WriteWord(unsigned address, unsigned value) {
    auto* bytes = reinterpret_cast<volatile unsigned char*>(address);
    for (unsigned i = 0; i < 4; ++i) bytes[i] = static_cast<unsigned char>(value >> (8U * i));
}
}
extern "C" int tls_guest_main() {
    Require(native_tls_record.nonce == GTOS_TLS_PROBE_VICTIM_NONCE || native_tls_record.nonce == GTOS_TLS_PROBE_PEER_NONCE, 7);
    Require(gtos_emutls_spec_count == 5, 8);
    const auto before = actual_v8_try();
    const auto invalid = actual_v8_invalid();
    Require(!actual_v8_valid(before) && actual_v8_equal(before, invalid) && actual_v8_integer(before) == -1, 9);
    native_tls_record.try_before = static_cast<unsigned>(actual_v8_integer(before));
    Observe(1);
    Require(*reinterpret_cast<volatile unsigned*>(native_tls_record.objects[0].address) == 0, 10);
    native_tls_record.stage = 1;
    Hold(32);
    native_tls_record.stage = 0;
    const auto current = actual_v8_current();
    const auto expected = actual_v8_from_integer(1);
    const auto after = actual_v8_try();
    const auto repeated = actual_v8_current();
    Require(actual_v8_valid(current) && actual_v8_integer(current) == 1 && actual_v8_equal(current, expected)
        && actual_v8_equal(after, current) && actual_v8_equal(repeated, current) && actual_v8_unequal(current, invalid), 11);
    native_tls_record.current = static_cast<unsigned>(actual_v8_integer(current));
    native_tls_record.try_after = static_cast<unsigned>(actual_v8_integer(after));
    native_tls_record.repeated = static_cast<unsigned>(actual_v8_integer(repeated));
    unsigned char* zero = actual_tls_zero();
    unsigned char* nonzero = actual_tls_nonzero();
    unsigned char* aligned = actual_tls_aligned();
    Initial(zero, 257, 1); Initial(nonzero, 64, 2); Initial(aligned, 64, 3);
    Fill(zero, 257, 1); Fill(nonzero, 64, 2); Fill(aligned, 64, 3);
    Observe(4);
    Require(Pointer(zero) == native_tls_record.objects[1].address && Pointer(nonzero) == native_tls_record.objects[2].address
        && Pointer(aligned) == native_tls_record.objects[3].address && !native_tls_record.objects[4].address, 12);
    native_tls_record.stage = 2;
    Hold(32);
    Pattern(actual_tls_zero(), 257, 1); Pattern(actual_tls_nonzero(), 64, 2); Pattern(actual_tls_aligned(), 64, 3);
    const auto resumed = actual_v8_try();
    Require(actual_v8_equal(resumed, current) && *reinterpret_cast<volatile unsigned*>(native_tls_record.objects[0].address) == 1, 13);
#if GTOS_TLS_PROBE_MODE == 3
    for (;;) Hold(1);
#elif GTOS_TLS_PROBE_MODE == 4
    native_tls_record.failed_slot = Slot(4);
    native_tls_record.last_request_control = Control(Slot(4));
    native_tls_record.unpublished_address = 0xA5A5A5A5U;
    native_tls_record.stage = 3;
    native_tls_record.unpublished_address = Pointer(actual_tls_oom());
    tls_probe_fail(14);
#elif GTOS_TLS_PROBE_MODE == 5 || GTOS_TLS_PROBE_MODE == 6
    native_tls_record.failed_slot = Slot(4);
    native_tls_record.last_request_control = Control(Slot(4));
    native_tls_record.unpublished_address = 0xA5A5A5A5U;
    native_tls_record.stage = 3;
#if GTOS_TLS_PROBE_MODE == 5
    WriteWord(Control(Slot(4)) + 4, 6);
#else
    WriteWord(Control(Slot(4)), 0xFFFFFFFFU);
#endif
    native_tls_record.unpublished_address = Pointer(actual_tls_oom());
    tls_probe_fail(15);
#elif GTOS_TLS_PROBE_MODE == 2
    native_tls_record.stage = 3;
    *reinterpret_cast<volatile unsigned*>(GTOS_TLS_PROBE_GUARD_VA) = 1;
    tls_probe_fail(16);
#elif GTOS_TLS_PROBE_MODE == 1
    native_tls_record.stage = 3;
    tls_probe_call(GTOS_SYS_EXIT, 0, 0);
    tls_probe_fail(17);
#else
    native_tls_record.stage = 0;
    native_tls_record.finalize_called = 1;
    gtos_emutls_finalize();
    gtos_emutls_finalize();
    GtosEmutlsInfo info{};
    Require(gtos_emutls_query(&info) == 1 && info.finalized == 1 && info.live == 0 && info.count == 5, 18);
    for (unsigned slot = 0; slot < 5; ++slot) Require(info.cached_address[slot] == 0, 19);
    GtosVmRegionInfo heap{};
    Require(gtos_native_heap_query(&heap) == 0, 20);
    native_tls_record.finalize_completed = 1;
    native_tls_record.heap_empty = 1;
    native_tls_record.cache_reset_count = 4;
    native_tls_record.initialized_count = 0;
    native_tls_record.final_heap_handle = 0;
    native_tls_record.stage = 3;
    return 0;
#endif
}
