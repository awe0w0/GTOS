#ifndef GTOS_PNG_DECODE_H
#define GTOS_PNG_DECODE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum gtos_png_status {
    GTOS_PNG_OK = 0,
    GTOS_PNG_BAD_ARGUMENT,
    GTOS_PNG_POINTER_OVERFLOW,
    GTOS_PNG_OVERLAP,
    GTOS_PNG_TOO_SMALL,
    GTOS_PNG_CORRUPT,
    GTOS_PNG_UNSUPPORTED,
    GTOS_PNG_DECODER_FAILURE
} gtos_png_status;

typedef struct gtos_png_requirements {
    uint32_t context_bytes;
    uint32_t work_bytes;
    uint32_t width;
    uint32_t height;
    uint32_t row_bytes;
    uint32_t bgra_bytes;
} gtos_png_requirements;

/* Caller owns accessible memory throughout the call. Context is 8-byte aligned;
 * requirements is naturally aligned. All supplied regions must be disjoint,
 * including requirements and the full declared capacities. Input is immutable.
 * No allocation, OS service, mutable global state, or CPU acceleration is used.
 * Static PNG formats supported by pinned Wuffs are decoded, including Adam7.
 * Ancillary metadata is CRC-checked but not interpreted or semantically certified.
 * APNG and unknown critical extensions return UNSUPPORTED after envelope checks;
 * this result does not certify their compressed payloads.
 */
uint32_t gtos_png_context_bytes(void);

/* Validates the complete envelope and configures actual Wuffs. It does not
 * decompress IDAT. Requirements is published only on success. Context may change
 * on a decoder failure; input and requirements never change on failure.
 */
gtos_png_status gtos_png_inspect(void* context, uint32_t context_bytes,
    const unsigned char* input, uint32_t input_bytes,
    gtos_png_requirements* requirements);

/* Decodes a complete static image to straight-alpha BGRA. requirements.bgra_bytes
 * is the packed minimum; padded storage needs (height-1)*stride + row_bytes.
 * Success requires complete zlib data, valid pixels, IEND, and exact input EOF.
 * Context, work, and active BGRA pixels may change on failure. Padding and unused
 * capacities are untouched. Requirements is published only on success.
 */
gtos_png_status gtos_png_decode_bgra(void* context, uint32_t context_bytes,
    const unsigned char* input, uint32_t input_bytes,
    unsigned char* work, uint32_t work_bytes,
    unsigned char* bgra, uint32_t bgra_bytes, uint32_t bgra_stride,
    gtos_png_requirements* requirements);

#ifdef __cplusplus
}
#endif
#endif
