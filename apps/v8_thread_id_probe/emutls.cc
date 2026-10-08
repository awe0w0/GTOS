#include "emutls.h"
#include "../native_heap/heap.h"
#include <process/abi.h>
#include <stddef.h>
#include <stdlib.h>

#if !defined(__i386__) || !defined(__GTOS__) || defined(__linux__) || defined(__unix__) || defined(_WIN32)
#error The native emutls resolver requires the GTOS i386 private-PAS profile
#endif
static_assert(sizeof(size_t) == 4, "GTOS IA32 emutls allocation ABI");
static_assert(__builtin_offsetof(GtosEmutlsControl, alignment) == 4
    && __builtin_offsetof(GtosEmutlsControl, cached_address) == 8
    && __builtin_offsetof(GtosEmutlsControl, initial_value) == 12, "Observed control fields");
namespace {
    const unsigned StaticBase = 0x40000000U, StaticLimit = 0x80000000U;
    const unsigned ArenaBase = 0x80000000U, ArenaLimit = 0xBFFFC000U;
    const unsigned MaximumPayload = GTOS_NATIVE_HEAP_BYTES - GTOS_NATIVE_HEAP_HEADER_BYTES;
    void* cache[GTOS_EMUTLS_MAX_OBJECTS];
    bool resolving, finalized;

    [[noreturn]] void Fail(unsigned reason) noexcept {
        const unsigned code = GTOS_EMUTLS_EXIT_BASE | reason;
        asm volatile("int $0x80" : : "a"(GTOS_SYS_EXIT), "b"(code), "c"(0) : "memory", "cc");
        asm volatile("ud2");
        __builtin_unreachable();
    }
    unsigned Address(const void* pointer) noexcept { return (unsigned)pointer; }
    bool PowerOfTwo(unsigned value) noexcept { return value && !(value & (value - 1U)); }
    unsigned Word(const unsigned char* control, unsigned offset) noexcept {
        const volatile unsigned char* bytes = control + offset;
        return (unsigned)bytes[0] | ((unsigned)bytes[1] << 8)
            | ((unsigned)bytes[2] << 16) | ((unsigned)bytes[3] << 24);
    }
    void StoreCache(unsigned char* control, unsigned address) noexcept {
        volatile unsigned char* bytes = control + 8;
        bytes[0] = (unsigned char)address;
        bytes[1] = (unsigned char)(address >> 8);
        bytes[2] = (unsigned char)(address >> 16);
        bytes[3] = (unsigned char)(address >> 24);
    }
    unsigned Inventory() noexcept {
        const unsigned start = Address(__gtos_emutls_controls_start);
        const unsigned end = Address(__gtos_emutls_controls_end);
        const unsigned templateStart = Address(__gtos_emutls_templates_start);
        const unsigned templateEnd = Address(__gtos_emutls_templates_end);
        const unsigned count = gtos_emutls_spec_count;
        if (start < StaticBase || end > StaticLimit || end < start || (start & 3U)
            || !count || count > GTOS_EMUTLS_MAX_OBJECTS
            || end - start != count * GTOS_EMUTLS_CONTROL_BYTES
            || templateStart < StaticBase || templateEnd > StaticLimit || templateEnd < templateStart)
            Fail(GTOS_EMUTLS_EXIT_REGISTRY);
        for (unsigned i = 0; i < count; ++i) {
            const GtosEmutlsSpec& spec = gtos_emutls_specs[i];
            const unsigned initial = Address(spec.initial_value);
            if (spec.control_offset != i * GTOS_EMUTLS_CONTROL_BYTES)
                Fail(GTOS_EMUTLS_EXIT_REGISTRY);
            if (!spec.size || spec.size > MaximumPayload || !PowerOfTwo(spec.alignment)
                || spec.alignment > GTOS_EMUTLS_MAX_ALIGNMENT)
                Fail(GTOS_EMUTLS_EXIT_METADATA);
            if (initial && (initial < templateStart || initial > templateEnd
                || spec.size > templateEnd - initial))
                Fail(GTOS_EMUTLS_EXIT_METADATA);
        }
        return count;
    }
    unsigned char* Control(unsigned slot) noexcept {
        return __gtos_emutls_controls_start + gtos_emutls_specs[slot].control_offset;
    }
    void Validate(unsigned count) noexcept {
        for (unsigned i = 0; i < count; ++i) {
            const GtosEmutlsSpec& spec = gtos_emutls_specs[i];
            const unsigned char* control = Control(i);
            if (Word(control, 0) != spec.size || Word(control, 4) != spec.alignment
                || Word(control, 12) != Address(spec.initial_value))
                Fail(GTOS_EMUTLS_EXIT_METADATA);
            const unsigned address = Address(cache[i]);
            if (Word(control, 8) != address) Fail(GTOS_EMUTLS_EXIT_CACHE);
            if (address && (address < ArenaBase || address > ArenaLimit - spec.size
                || (address & (spec.alignment - 1U))))
                Fail(GTOS_EMUTLS_EXIT_CACHE);
            if (address) {
                for (unsigned j = 0; j < i; ++j) {
                    const unsigned other = Address(cache[j]);
                    if (other && (address < other
                        ? spec.size > other - address : gtos_emutls_specs[j].size > address - other))
                        Fail(GTOS_EMUTLS_EXIT_CACHE);
                }
            }
        }
        for (unsigned i = count; i < GTOS_EMUTLS_MAX_OBJECTS; ++i)
            if (cache[i]) Fail(GTOS_EMUTLS_EXIT_CACHE);
    }
    void Enter() noexcept {
        if (resolving) Fail(GTOS_EMUTLS_EXIT_RECURSION);
        resolving = true;
    }
}
extern "C" void* __emutls_get_address(void* requested) noexcept {
    if (finalized) Fail(GTOS_EMUTLS_EXIT_FINALIZED);
    Enter();
    const unsigned count = Inventory();
    unsigned slot = count;
    for (unsigned i = 0; i < count; ++i)
        if (Address(requested) == Address(Control(i))) { slot = i; break; }
    if (slot == count) Fail(GTOS_EMUTLS_EXIT_UNKNOWN_CONTROL);
    // Exact membership precedes every compiler-control read.
    Validate(count);
    if (!cache[slot]) {
        const GtosEmutlsSpec& spec = gtos_emutls_specs[slot];
        void* object = nullptr;
        const unsigned alignment = spec.alignment < sizeof(void*) ? sizeof(void*) : spec.alignment;
        if (posix_memalign(&object, alignment, spec.size) != 0 || !object)
            Fail(GTOS_EMUTLS_EXIT_OOM);
        const unsigned address = Address(object);
        if (address < ArenaBase || address > ArenaLimit - spec.size
            || (address & (alignment - 1U)))
            Fail(GTOS_EMUTLS_EXIT_CACHE);
        for (unsigned i = 0; i < count; ++i) {
            const unsigned other = Address(cache[i]);
            if (other && (address < other
                ? spec.size > other - address : gtos_emutls_specs[i].size > address - other))
                Fail(GTOS_EMUTLS_EXIT_CACHE);
        }
        volatile unsigned char* output = (volatile unsigned char*)object;
        for (unsigned i = 0; i < spec.size; ++i)
            output[i] = spec.initial_value ? spec.initial_value[i] : 0;
        // The single Task owns both caches; no asynchronous resolver or
        // same-PAS peer can observe a partly published address.
        asm volatile("" : : : "memory");
        cache[slot] = object;
        StoreCache(Control(slot), address);
    }
    void* result = cache[slot];
    resolving = false;
    return result;
}
extern "C" void gtos_emutls_finalize() noexcept {
    Enter();
    const unsigned count = Inventory();
    Validate(count);
    if (!finalized) {
        for (unsigned i = 0; i < count; ++i) {
            if (!cache[i]) continue;
            free(cache[i]);
            cache[i] = nullptr;
            StoreCache(Control(i), 0);
        }
        finalized = true;
    }
    resolving = false;
}
extern "C" int gtos_emutls_query(GtosEmutlsInfo* output) noexcept {
    if (!output) return 0;
    Enter();
    const unsigned count = Inventory();
    Validate(count);
    output->version = GTOS_EMUTLS_ABI_VERSION;
    output->count = count;
    output->live = 0;
    output->finalized = finalized ? 1U : 0U;
    for (unsigned i = 0; i < GTOS_EMUTLS_MAX_OBJECTS; ++i) {
        output->cached_address[i] = Address(cache[i]);
        if (cache[i]) ++output->live;
    }
    resolving = false;
    return 1;
}