#ifndef GTOS_PROCESS_RESOURCE_ABI_H
#define GTOS_PROCESS_RESOURCE_ABI_H

// Additive resource extension for the existing GTOS i386 native ABI 1.
// int 0x80: EAX=call, EBX/ECX=arguments, EAX=result. Other registers survive.
// INFO: EBX=resource ID, ECX=user destination for exactly 16 bytes; returns 0.
// READ: EBX=user request, ECX=exact request size (20); returns bytes copied.
// Negative signed 32-bit results are errors. No addresses or handles are returned.
#define GTOS_SYS_RESOURCE_INFO 0x4705U
#define GTOS_SYS_RESOURCE_READ 0x4706U
#define GTOS_RESOURCE_ABI_VERSION 1U
#define GTOS_RESOURCE_PUBLIC_PNG_ID 1U
#define GTOS_RESOURCE_TYPE_PNG 1U
#define GTOS_RESOURCE_FLAG_READONLY 1U
#define GTOS_RESOURCE_FLAG_PUBLIC 2U
#define GTOS_RESOURCE_READ_LIMIT 256U
#define GTOS_RESOURCE_INFO_BYTES 16U
#define GTOS_RESOURCE_READ_REQUEST_BYTES 20U
#define GTOS_RESOURCE_ERR_NOT_FOUND (-2)
#define GTOS_RESOURCE_ERR_TOO_LARGE (-7)
#define GTOS_RESOURCE_ERR_BAD_ADDRESS (-14)
#define GTOS_RESOURCE_ERR_BAD_SIZE (-22)
#define GTOS_RESOURCE_ERR_RANGE (-34)
#define GTOS_RESOURCE_ERR_UNSUPPORTED_VERSION (-38)

// Unsigned fields are exactly 32 bits in both the i386 ABI and host checks.
// Keep these records free of native pointers, padding and C++ library types.
typedef struct GtosResourceInfo {
    unsigned version, type, bytes, flags;
} GtosResourceInfo;
typedef struct GtosResourceReadRequest {
    unsigned version, id, offset, destination, length;
} GtosResourceReadRequest;

// READ checks size before copying the entire request into a kernel snapshot.
// A readable snapshot is checked in order: version, ID, length cap, offset,
// then writable output. All errors leave user output unchanged. INFO checks
// ID before writable output. Request/output overlap is allowed after snapshot.
// offset == resource bytes is EOF; offset beyond EOF is RANGE. Short reads
// validate only actual bytes. Zero-byte output still requires a destination
// inside [0x40000000,0xC0000000), but need not refer to a mapped page.
#if defined(__cplusplus)
static_assert(sizeof(unsigned) == 4, "Resource ABI requires 32-bit unsigned");
static_assert(sizeof(GtosResourceInfo) == GTOS_RESOURCE_INFO_BYTES, "Resource info ABI");
static_assert(sizeof(GtosResourceReadRequest) == GTOS_RESOURCE_READ_REQUEST_BYTES, "Resource read ABI");
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(unsigned) == 4, "Resource ABI requires 32-bit unsigned");
_Static_assert(sizeof(GtosResourceInfo) == GTOS_RESOURCE_INFO_BYTES, "Resource info ABI");
_Static_assert(sizeof(GtosResourceReadRequest) == GTOS_RESOURCE_READ_REQUEST_BYTES, "Resource read ABI");
#endif
#endif
