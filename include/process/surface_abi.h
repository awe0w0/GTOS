#ifndef GTOS_PROCESS_SURFACE_ABI_H
#define GTOS_PROCESS_SURFACE_ABI_H

// Additive, separately versioned extension of GTOS i386 native ABI 1.
// int 0x80: EAX=call, EBX=user request, ECX=exact request bytes.
// EAX=result; every other general and segment register is preserved.
// BEGIN returns a positive, never-reused handle; WRITE returns bytes copied.
// PRESENT and ABORT return 0.
// Negative signed 32-bit results are errors. No kernel address is exposed.
#define GTOS_SYS_SURFACE_BEGIN 0x4707U
#define GTOS_SYS_SURFACE_WRITE 0x4708U
#define GTOS_SYS_SURFACE_PRESENT 0x4709U
#define GTOS_SYS_SURFACE_ABORT 0x470aU
#define GTOS_SURFACE_ABI_VERSION 1U
#define GTOS_SURFACE_FORMAT_RGBA8_PREMULTIPLIED 1U
#define GTOS_SURFACE_MAX_WIDTH 32U
#define GTOS_SURFACE_MAX_HEIGHT 32U
#define GTOS_SURFACE_MAX_BYTES 4096U
#define GTOS_SURFACE_WRITE_LIMIT 256U
#define GTOS_SURFACE_BEGIN_REQUEST_BYTES 20U
#define GTOS_SURFACE_WRITE_REQUEST_BYTES 20U
#define GTOS_SURFACE_CONTROL_REQUEST_BYTES 8U
#define GTOS_SURFACE_ERR_TOO_LARGE (-7)
#define GTOS_SURFACE_ERR_BAD_ADDRESS (-14)
#define GTOS_SURFACE_ERR_BUSY (-16)
#define GTOS_SURFACE_ERR_UNAVAILABLE (-19)
#define GTOS_SURFACE_ERR_BAD_SIZE (-22)
#define GTOS_SURFACE_ERR_BAD_STATE (-22)
#define GTOS_SURFACE_ERR_RANGE (-34)
#define GTOS_SURFACE_ERR_UNSUPPORTED_VERSION (-38)
#define GTOS_SURFACE_ERR_UNSUPPORTED_FORMAT (-38)
#define GTOS_SURFACE_ERR_BAD_PIXELS (-84)

typedef struct GtosSurfaceBeginRequest {
    unsigned version, width, height, stride, format;
} GtosSurfaceBeginRequest;
typedef struct GtosSurfaceWriteRequest {
    unsigned version, handle, offset, source, length;
} GtosSurfaceWriteRequest;
typedef struct GtosSurfaceControlRequest {
    unsigned version, handle;
} GtosSurfaceControlRequest;

// All descriptors are snapshotted with checked-copy after exact wire-size
// validation. BEGIN checks version, current owner, display availability,
// format, dimensions, exact stride, transaction availability, then handle
// exhaustion. Dimensions are bounded before either multiplication.
// WRITE checks version and live owner/handle, length (1..256 and multiple
// of 4), exact sequential offset and remaining bytes before checked-copy.
// The entire copied chunk is checked for R/G/B <= A before any mutation.
// Readable read-only user sources and request/source overlap are accepted.
// PRESENT requires a complete transaction. Success publishes a desktop-owned
// immutable RGBA snapshot whose generation equals the retired handle.
// ABORT, process exit/fault/reap scrub only the owner's unfinished draft.
// Published pixels survive producer exit; closing the GUI hides the image.
// Every failed call preserves the draft, handle sequence and published frame.

#if defined(__cplusplus)
static_assert(sizeof(unsigned) == 4, "Surface ABI requires 32-bit unsigned");
static_assert(sizeof(GtosSurfaceBeginRequest) == GTOS_SURFACE_BEGIN_REQUEST_BYTES, "Surface begin ABI");
static_assert(sizeof(GtosSurfaceWriteRequest) == GTOS_SURFACE_WRITE_REQUEST_BYTES, "Surface write ABI");
static_assert(sizeof(GtosSurfaceControlRequest) == GTOS_SURFACE_CONTROL_REQUEST_BYTES, "Surface control ABI");
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(unsigned) == 4, "Surface ABI requires 32-bit unsigned");
_Static_assert(sizeof(GtosSurfaceBeginRequest) == GTOS_SURFACE_BEGIN_REQUEST_BYTES, "Surface begin ABI");
_Static_assert(sizeof(GtosSurfaceWriteRequest) == GTOS_SURFACE_WRITE_REQUEST_BYTES, "Surface write ABI");
_Static_assert(sizeof(GtosSurfaceControlRequest) == GTOS_SURFACE_CONTROL_REQUEST_BYTES, "Surface control ABI");
#endif
#endif
