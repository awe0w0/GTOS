#ifndef GTOS_PNG_PIXEL_H
#define GTOS_PNG_PIXEL_H
#include "png_decode.h"
#ifdef __cplusplus
extern "C" {
#endif
// Caller owns accessible buffers. All declared regions must be disjoint.
// Output is premultiplied RGBA. It and requirements are published only after
// full PNG validation; BGRA is caller-owned straight-alpha scratch storage.
gtos_png_status gtos_png_decode_rgba(
    void* context, uint32_t context_bytes,
    const uint8_t* input, uint32_t input_bytes,
    uint8_t* work, uint32_t work_bytes,
    uint8_t* bgra, uint32_t bgra_bytes, uint32_t bgra_stride,
    uint8_t* rgba, uint32_t rgba_bytes, uint32_t rgba_stride,
    gtos_png_requirements* requirements);
#ifdef __cplusplus
}
#endif
#endif
