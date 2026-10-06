#ifndef GTOS_GUI_MODERN_PAINTER_H
#define GTOS_GUI_MODERN_PAINTER_H
#include <drivers/framebuffer.h>
namespace gtos {
namespace gui {
class ModernPainter {
    drivers::Framebuffer &fb;

  public:
    explicit ModernPainter(drivers::Framebuffer &framebuffer) : fb(framebuffer) {}
    void Text(int32_t x, int32_t y, const char *text, uint32_t color, uint32_t scale = 1);
    int32_t TextWidth(const char *text, uint32_t scale = 1) const;
    void Number(int32_t x, int32_t y, uint32_t value, uint32_t color, uint32_t scale = 1);
    void Rounded(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint32_t color);
    void Outline(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
    void Icon(int32_t x, int32_t y, uint32_t kind, uint32_t color, uint32_t scale = 1);
};
const uint8_t *ModernGameGlyph(char c);
} // namespace gui
} // namespace gtos
#endif
