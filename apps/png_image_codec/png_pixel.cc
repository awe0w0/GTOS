#include "png_pixel.h"
#include "skia_pixel.h"

#if defined(GTOS_NATIVE_COMPONENT) && (!defined(__i386__) || defined(__linux__))
#error Native i386 non-Linux required
#endif

namespace {
struct Region { uintptr_t begin, end; };
gtos_png_status region(const void* p, uint32_t bytes, Region* r) {
    if (!p && bytes) return GTOS_PNG_BAD_ARGUMENT;
    uintptr_t begin = reinterpret_cast<uintptr_t>(p);
    if (bytes > UINTPTR_MAX - begin) return GTOS_PNG_POINTER_OVERFLOW;
    r->begin = begin; r->end = begin + bytes;
    return GTOS_PNG_OK;
}
bool overlaps(const Region& a, const Region& b) {
    return a.begin != a.end && b.begin != b.end &&
           a.begin < b.end && b.begin < a.end;
}
}

extern "C" gtos_png_status gtos_png_decode_rgba(
    void* context, uint32_t context_bytes,
    const uint8_t* input, uint32_t input_bytes,
    uint8_t* work, uint32_t work_bytes,
    uint8_t* bgra, uint32_t bgra_bytes, uint32_t bgra_stride,
    uint8_t* rgba, uint32_t rgba_bytes, uint32_t rgba_stride,
    gtos_png_requirements* requirements) {
    if (!context || !input || !input_bytes || !bgra || !rgba || !requirements ||
        (reinterpret_cast<uintptr_t>(context) & 7) ||
        (reinterpret_cast<uintptr_t>(requirements) & (alignof(gtos_png_requirements)-1)))
        return GTOS_PNG_BAD_ARGUMENT;
    Region regions[6];
    const void* pointers[6] = {context,input,work,bgra,rgba,requirements};
    const uint32_t sizes[6] = {context_bytes,input_bytes,work_bytes,bgra_bytes,
                              rgba_bytes,sizeof(*requirements)};
    for (unsigned i=0;i<6;++i) {
        gtos_png_status status=region(pointers[i],sizes[i],&regions[i]);
        if (status != GTOS_PNG_OK) return status;
    }
    for (unsigned i=0;i<6;++i)
        for (unsigned j=i+1;j<6;++j)
            if (overlaps(regions[i],regions[j])) return GTOS_PNG_OVERLAP;
    if (context_bytes < gtos_png_context_bytes()) return GTOS_PNG_TOO_SMALL;
    gtos_png_requirements needed;
    gtos_png_status status=gtos_png_inspect(context,context_bytes,input,input_bytes,&needed);
    if (status != GTOS_PNG_OK) return status;
    if (bgra_stride < needed.row_bytes || rgba_stride < needed.row_bytes)
        return GTOS_PNG_BAD_ARGUMENT;
    const uint64_t bgra_span=uint64_t(needed.height-1)*bgra_stride+needed.row_bytes;
    const uint64_t rgba_span=uint64_t(needed.height-1)*rgba_stride+needed.row_bytes;
    if (bgra_span > bgra_bytes || rgba_span > rgba_bytes || work_bytes < needed.work_bytes)
        return GTOS_PNG_TOO_SMALL;
    status=gtos_png_decode_bgra(context,context_bytes,input,input_bytes,work,work_bytes,
                               bgra,bgra_bytes,bgra_stride,&needed);
    if (status != GTOS_PNG_OK) return status;
    // Every condition of the existing Skia leaf was checked above. It validates
    // again before writing, so even an unexpected leaf failure preserves RGBA.
    const gtos_skia::PixelResult converted=gtos_skia::PremultiplyBgraRows(
        bgra,bgra_bytes,bgra_stride,rgba,rgba_bytes,rgba_stride,needed.width,needed.height);
    if (converted != gtos_skia::PixelOk) return GTOS_PNG_DECODER_FAILURE;
    *requirements=needed;
    return GTOS_PNG_OK;
}
