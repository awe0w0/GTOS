#ifndef GTOS_PROCESS_ENVIRONMENT_ABI_H
#define GTOS_PROCESS_ENVIRONMENT_ABI_H
// Immutable startup environment of the calling process, shared by its threads.
// EAX=call, EBX=request VA, ECX=exact bytes. EAX=0 or a negative error.
#define GTOS_ENVIRONMENT_ABI_VERSION 1U
#define GTOS_SYS_ENVIRONMENT_READ 0x4717U
#define GTOS_ENVIRONMENT_REQUEST_BYTES 16U
#define GTOS_ENVIRONMENT_RESULT_BYTES 4096U
#define GTOS_ENVIRONMENT_MAXIMUM_ENTRIES 64U
#define GTOS_ENVIRONMENT_DATA_BYTES 3824U
#define GTOS_ENVIRONMENT_ERR_BAD_SIZE (-22)
#define GTOS_ENVIRONMENT_ERR_BAD_ADDRESS (-14)
#define GTOS_ENVIRONMENT_ERR_UNSUPPORTED (-38)

typedef struct GtosEnvironmentReadRequest {
    unsigned version, flags, result_va, result_bytes;
} GtosEnvironmentReadRequest;
typedef struct GtosEnvironmentReadResult {
    unsigned version, count, data_bytes, reserved;
    unsigned offsets[GTOS_ENVIRONMENT_MAXIMUM_ENTRIES];
    char data[GTOS_ENVIRONMENT_DATA_BYTES];
} GtosEnvironmentReadResult;

// Entries are consecutive NUL-terminated, unique, nonempty-name NAME=VALUE
// strings, preserving launch order. Empty values and '=' inside values work.
// Offsets are relative to data; unused offsets/data and reserved are zero.
// There is no target ID or user update API. No host environment is imported.
// Error order: ECX, whole request copy, version, flags/result_bytes, whole
// writable output validation, checked copy. Overlap and unaligned output work.
// Failure changes no output byte. Read allocates nothing and does not yield.
#if defined(__cplusplus)
static_assert(sizeof(unsigned) == 4, "Environment ABI requires 32-bit unsigned");
static_assert(sizeof(GtosEnvironmentReadRequest) == GTOS_ENVIRONMENT_REQUEST_BYTES, "Environment request size");
static_assert(__builtin_offsetof(GtosEnvironmentReadRequest, result_va) == 8, "Environment output address");
static_assert(sizeof(GtosEnvironmentReadResult) == GTOS_ENVIRONMENT_RESULT_BYTES, "Environment result size");
static_assert(__builtin_offsetof(GtosEnvironmentReadResult, offsets) == 16, "Environment offsets");
static_assert(__builtin_offsetof(GtosEnvironmentReadResult, data) == 272, "Environment data");
#endif
#endif
