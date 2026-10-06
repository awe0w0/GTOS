#ifndef GTOS_SKIA_PIXEL_H
#define GTOS_SKIA_PIXEL_H
#include <stdint.h>
extern "C" void gtos_skia_assert_failure(void) __attribute__((noreturn));
namespace gtos_skia {
static_assert(sizeof(unsigned)==4 && sizeof(uint8_t)==1, "32-bit scalar pixel ABI");
typedef unsigned U8CPU;
typedef unsigned U16CPU;
#define SkASSERT(condition) do { if (!(condition)) gtos_skia_assert_failure(); } while (false)
#include "skia_alpha_upstream.inc"
#undef SkASSERT
enum PixelResult { PixelOk=0, BadBuffer=-1, BadGeometry=-2, BadStride=-3,
                   TooShort=-4, Overlap=-5, PointerOverflow=-6 };
// Caller-owned accessible buffers only; no allocation, OS mapping or submission.
// Input BGRA is straight alpha; output RGBA is premultiplied alpha.
PixelResult PremultiplyBgraRows(const uint8_t* src, uint32_t srcBytes, uint32_t srcStride,
                              uint8_t* dst, uint32_t dstBytes, uint32_t dstStride,
                              uint32_t width, uint32_t height);
}
#endif
