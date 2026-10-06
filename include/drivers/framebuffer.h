#ifndef GTOS_DRIVERS_FRAMEBUFFER_H
#define GTOS_DRIVERS_FRAMEBUFFER_H
#include <memory/multiboot.h>
namespace gtos {
namespace drivers {
static_assert(__builtin_offsetof(memory::MultibootInfo, framebufferColorInfo) == 112,
              "GRUB Multiboot v1 RGB descriptor offset");
// Colors in the RAM surface are always 0x00RRGGBB. Conversion to hardware masks
// occurs only when Present() copies the bounded dirty region to video memory.
struct FramebufferMode {
    uint32_t width, height, pitch;
    uint8_t redPosition, redSize, greenPosition, greenSize, bluePosition, blueSize;
};
class Framebuffer {
    FramebufferMode mode;
    volatile uint8_t *video;
    uint32_t *pixels;
    int32_t clipX, clipY, clipR, clipB, dirtyX, dirtyY, dirtyR, dirtyB;
    bool ready, nativeRGB;
    void Damage(int32_t x, int32_t y, int32_t r, int32_t b);

  public:
    Framebuffer();
    static bool Validate(const FramebufferMode &mode);
    static bool ReadMode(const memory::MultibootInfo *info, FramebufferMode &mode);
    bool Configure(const memory::MultibootInfo *info, uint32_t *backing, uint32_t backingPixels);
    // Explicit memory binding is also used by host regression tests.
    bool Bind(const FramebufferMode &mode, volatile uint8_t *video, uint32_t *backing,
              uint32_t backingPixels);
    bool Ready() const { return ready; }
    uint32_t Width() const { return ready ? mode.width : 0; }
    uint32_t Height() const { return ready ? mode.height : 0; }
    uint32_t Pitch() const { return ready ? mode.pitch : 0; }
    void SetClip(int32_t x, int32_t y, int32_t w, int32_t h);
    void ResetClip();
    void Pixel(int32_t x, int32_t y, uint32_t color);
    void Blend(int32_t x, int32_t y, uint32_t color, uint8_t alpha);
    uint32_t ReadPixel(int32_t x, int32_t y) const;
    void Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
    void Clear(uint32_t color);
    void Present();
};
} // namespace drivers
} // namespace gtos
#endif
