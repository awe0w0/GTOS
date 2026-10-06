#ifndef GTOS_GUI_CANVAS_H
#define GTOS_GUI_CANVAS_H
#include <common/types.h>
namespace gtos {
namespace gui {
class Canvas {
    uint8_t pixels[320 * 200];

  public:
    void Clear(uint8_t color);
    void Pixel(int32_t x, int32_t y, uint8_t color);
    void Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color);
    void Frame(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color);
    void Text(int32_t x, int32_t y, const char *text, uint8_t color, uint32_t scale = 1);
    void Number(int32_t x, int32_t y, uint32_t value, uint8_t color);
    void Blit(const Canvas &source, int32_t sx, int32_t sy, int32_t w, int32_t h, int32_t dx,
              int32_t dy);
    void Present();
};
} // namespace gui
} // namespace gtos
#endif
