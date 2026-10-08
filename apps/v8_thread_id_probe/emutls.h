#ifndef GTOS_NATIVE_EMUTLS_H
#define GTOS_NATIVE_EMUTLS_H

#define GTOS_EMUTLS_ABI_VERSION 1U
#define GTOS_EMUTLS_MAX_OBJECTS 8U
#define GTOS_EMUTLS_CONTROL_BYTES 16U
#define GTOS_EMUTLS_MAX_ALIGNMENT 65536U
#define GTOS_EMUTLS_EXIT_BASE 0x4A000000U
#define GTOS_EMUTLS_EXIT_REGISTRY 1U
#define GTOS_EMUTLS_EXIT_UNKNOWN_CONTROL 2U
#define GTOS_EMUTLS_EXIT_METADATA 3U
#define GTOS_EMUTLS_EXIT_CACHE 4U
#define GTOS_EMUTLS_EXIT_RECURSION 5U
#define GTOS_EMUTLS_EXIT_OOM 6U
#define GTOS_EMUTLS_EXIT_FINALIZED 7U

// Observed Clang24 i386 compiler ABI. Controls remain compiler-owned, including
// V8's private local symbol; the resolver reads their representation as bytes.
struct GtosEmutlsControl {
    unsigned size, alignment, cached_address, initial_value;
};
struct GtosEmutlsSpec {
    unsigned control_offset, size, alignment;
    const unsigned char* initial_value;
};
struct GtosEmutlsInfo {
    unsigned version, count, live, finalized;
    unsigned cached_address[GTOS_EMUTLS_MAX_OBJECTS];
};
static_assert(sizeof(unsigned) == 4 && sizeof(void*) == 4, "GTOS IA32 emutls ABI");
static_assert(sizeof(GtosEmutlsControl) == GTOS_EMUTLS_CONTROL_BYTES
    && alignof(GtosEmutlsControl) == 4, "Observed compiler control layout");
static_assert(sizeof(GtosEmutlsSpec) == 16 && sizeof(GtosEmutlsInfo) == 48, "Native emutls inventory");

// The builder binds this immutable exact-slot inventory and these linker
// ranges to every actual compiler object. No caller can register a descriptor.
extern "C" {
    extern unsigned char __gtos_emutls_controls_start[], __gtos_emutls_controls_end[];
    extern const unsigned char __gtos_emutls_templates_start[], __gtos_emutls_templates_end[];
    extern const GtosEmutlsSpec gtos_emutls_specs[];
    extern const unsigned gtos_emutls_spec_count;

    void* __emutls_get_address(void* control) noexcept;
    // Trivial storage cleanup only: no dynamic initialization or destructors.
    // Idempotent normal finalization frees every owned payload; later TLS
    // resolution terminates. Abrupt exits rely on private-PAS kernel Reap.
    void gtos_emutls_finalize() noexcept;
    // Diagnostics, not an independent oracle. Output must be a valid writable
    // caller-owned object outside controls, registry and TLS payload storage.
    // Null output returns 0. This function does not allocate storage.
    int gtos_emutls_query(GtosEmutlsInfo* output) noexcept;
}
// Single user Task per private PAS; no shared-PAS threads, native FS/GS TLS,
// platform-key TLS, TLS constructor/destructor registration, or heap locks.
#endif