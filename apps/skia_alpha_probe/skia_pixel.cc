#include "skia_pixel.h"
namespace gtos_skia {
PixelResult PremultiplyBgraRows(const uint8_t* src, uint32_t srcBytes, uint32_t srcStride,
                              uint8_t* dst, uint32_t dstBytes, uint32_t dstStride,
                              uint32_t width, uint32_t height) {
    if (!src || !dst) return BadBuffer;
    const uint64_t rowBytes=(uint64_t)width*4;
    if (!width || !height || rowBytes>UINT32_MAX) return BadGeometry;
    if (srcStride<rowBytes || dstStride<rowBytes) return BadStride;
    const uint64_t srcSpan=(uint64_t)(height-1)*srcStride+rowBytes;
    const uint64_t dstSpan=(uint64_t)(height-1)*dstStride+rowBytes;
    if (srcSpan>srcBytes || dstSpan>dstBytes) return TooShort;
    const uintptr_t from=(uintptr_t)src, to=(uintptr_t)dst;
    if (srcSpan>UINTPTR_MAX-from || dstSpan>UINTPTR_MAX-to) return PointerOverflow;
    if (from<to+(uintptr_t)dstSpan && to<from+(uintptr_t)srcSpan) return Overlap;
    for (uint32_t y=0;y<height;++y) {
        const uint8_t* input=src+y*srcStride;
        uint8_t* output=dst+y*dstStride;
        for (uint32_t x=0;x<width;++x) {
            const uint8_t* p=input+x*4;uint8_t* q=output+x*4;
            const unsigned alpha=p[3];
            q[0]=(uint8_t)SkMulDiv255Round(p[2],alpha);
            q[1]=(uint8_t)SkMulDiv255Round(p[1],alpha);
            q[2]=(uint8_t)SkMulDiv255Round(p[0],alpha);
            q[3]=(uint8_t)alpha;
        }
    }
    return PixelOk;
}
}
