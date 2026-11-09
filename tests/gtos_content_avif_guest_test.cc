#include <memory>
#include <stdint.h>
#include <string.h>

#include "avif/avif.h"

namespace avif = crabbyavif;

extern "C" {
extern const unsigned char gtos_avif_fixture[];
extern const unsigned char gtos_avif_fixture_end[];
}

int TestAvif() {
    const char text[] = "  Flags: sse2";
    if (strpbrk(text, ":=") != text + 7 || strpbrk(text, "=") || strpbrk(text, ""))
        return 83;

    constexpr size_t kBytes = 65537;
    auto* allocated = static_cast<unsigned char*>(avif::crabby_avifAlloc(kBytes));
    if (!allocated) return 84;
    bool zeroed = true;
    for (size_t i = 0; i < kBytes; ++i)
        if (allocated[i] != 0) zeroed = false;
    allocated[kBytes - 1] = 17;
    avif::crabby_avifFree(allocated);
    if (!zeroed) return 85;
    if (void* oversized = avif::crabby_avifAlloc(size_t{1} << 31)) {
        avif::crabby_avifFree(oversized);
        return 86;
    }

    std::unique_ptr<avif::avifDecoder, decltype(&avif::crabby_avifDecoderDestroy)>
        decoder(avif::crabby_avifDecoderCreate(), avif::crabby_avifDecoderDestroy);
    std::unique_ptr<avif::avifImage, decltype(&avif::crabby_avifImageDestroy)>
        image(avif::crabby_avifImageCreateEmpty(), avif::crabby_avifImageDestroy);
    if (!decoder || !image) return 87;
    decoder->maxThreads = 1;
    const size_t bytes = reinterpret_cast<uintptr_t>(gtos_avif_fixture_end) -
                         reinterpret_cast<uintptr_t>(gtos_avif_fixture);
    if (avif::crabby_avifDecoderReadMemory(decoder.get(), image.get(), gtos_avif_fixture, 16) ==
            avif::AVIF_RESULT_OK ||
        avif::crabby_avifDecoderReadMemory(decoder.get(), image.get(), gtos_avif_fixture, bytes) !=
            avif::AVIF_RESULT_OK ||
        image->width != 2 || image->height != 2 || image->depth != 8)
        return 88;

    avif::avifRGBImage rgb{};
    avif::crabby_avifRGBImageSetDefaults(&rgb, image.get());
    rgb.format = avif::AVIF_RGB_FORMAT_RGB;
    unsigned char pixels[2 * 2 * 3]{};
    rgb.pixels = pixels;
    rgb.rowBytes = 6;
    if (avif::crabby_avifImageYUVToRGB(image.get(), &rgb) != avif::AVIF_RESULT_OK) return 89;
    for (unsigned char component : pixels)
        if (component < 250) return 90;
    return 0;
}
