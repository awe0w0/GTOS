#include "modern_font_data.inc"
#include "modern_game_font.inc"
#include <gui/modern_painter.h>
using namespace gtos::gui;
void ModernPainter::Text(int32_t x, int32_t y, const char *t, uint32_t c, uint32_t scale) {
    if (!t || scale < 1 || scale > 3)
        return;
    int64_t px = x, py = y;
    for (uint32_t n = 0; n < 160 && t[n]; ++n) {
        uint8_t ch = (uint8_t)t[n];
        if (ch == '\n') {
            px = x;
            py += 20 * scale;
            continue;
        }
        if (ch < 32 || ch > 126)
            ch = '?';
        if (px >= -(int64_t)16 * scale && px < (int64_t)fb.Width() && py >= -(int64_t)18 * scale &&
            py < (int64_t)fb.Height()) {
            const uint8_t *glyph =
                scale == 2 ? modernFontPixelsLarge[ch - 32] : modernFontPixels[ch - 32];
            uint32_t raster = scale == 2 ? 2 : 1, zoom = scale / raster;
            for (uint32_t r = 0; r < 18 * raster; ++r)
                for (uint32_t b = 0; b < 16 * raster; ++b) {
                    uint32_t at = r * 16 * raster + b;
                    uint8_t a = (at & 1) ? glyph[at / 2] & 15 : glyph[at / 2] >> 4;
                    if (!a)
                        continue;
                    for (uint32_t sy = 0; sy < zoom; ++sy)
                        for (uint32_t sx = 0; sx < zoom; ++sx)
                            fb.Blend((int32_t)px + b * zoom + sx, (int32_t)py + r * zoom + sy, c,
                                     a);
                }
        }
        px += scale == 2 ? modernFontAdvanceLarge[ch - 32] : modernFontAdvance[ch - 32] * scale;
    }
}
int32_t ModernPainter::TextWidth(const char *t, uint32_t scale) const {
    if (!t || scale < 1 || scale > 3)
        return 0;
    int32_t w = 0;
    for (uint32_t i = 0; i < 160 && t[i] && t[i] != '\n'; ++i) {
        uint8_t ch = (uint8_t)t[i];
        if (ch < 32 || ch > 126)
            ch = '?';
        w += scale == 2 ? modernFontAdvanceLarge[ch - 32] : modernFontAdvance[ch - 32] * scale;
    }
    return w;
}
void ModernPainter::Number(int32_t x, int32_t y, uint32_t v, uint32_t c, uint32_t s) {
    char out[11], rev[10];
    uint32_t n = 0;
    do {
        rev[n++] = '0' + v % 10;
        v /= 10;
    } while (v);
    for (uint32_t i = 0; i < n; ++i)
        out[i] = rev[n - 1 - i];
    out[n] = 0;
    Text(x, y, out, c, s);
}
void ModernPainter::Rounded(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius,
                            uint32_t c) {
    if (w <= 0 || h <= 0 || x >= (int32_t)fb.Width() || y >= (int32_t)fb.Height() ||
        (int64_t)x + w <= 0 || (int64_t)y + h <= 0)
        return;
    if (radius < 0)
        radius = 0;
    if (radius > 12)
        radius = 12;
    if (radius > w / 2)
        radius = w / 2;
    if (radius > h / 2)
        radius = h / 2;
    fb.Rect(x, y + radius, w, h - 2 * radius, c);
    for (int32_t row = 0; row < radius; ++row) {
        int32_t inset = 0, dy = radius - row - 1;
        while (inset < radius &&
               (radius - inset - 1) * (radius - inset - 1) + dy * dy > radius * radius)
            ++inset;
        fb.Rect(x + inset, y + row, w - 2 * inset, 1, c);
        int64_t bottom = (int64_t)y + h - 1 - row;
        if (bottom >= 0 && bottom < (int64_t)fb.Height())
            fb.Rect(x + inset, (int32_t)bottom, w - 2 * inset, 1, c);
    }
}
void ModernPainter::Outline(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
    if (w <= 0 || h <= 0)
        return;
    fb.Rect(x, y, w, 1, c);
    int64_t bottom = (int64_t)y + h - 1, right = (int64_t)x + w - 1;
    if (bottom >= 0 && bottom < (int64_t)fb.Height())
        fb.Rect(x, (int32_t)bottom, w, 1, c);
    fb.Rect(x, y, 1, h, c);
    if (right >= 0 && right < (int64_t)fb.Width())
        fb.Rect((int32_t)right, y, 1, h, c);
}
void ModernPainter::Icon(int32_t x, int32_t y, uint32_t k, uint32_t c, uint32_t s) {
    if (s < 1 || s > 3 || x >= (int32_t)fb.Width() || y >= (int32_t)fb.Height() ||
        (int64_t)x + 18 * s <= 0 || (int64_t)y + 18 * s <= 0)
        return;
    if (k == 0) {
        for (uint32_t i = 0; i < 4; ++i)
            fb.Rect(x + (i % 2) * 9 * s, y + (i / 2) * 9 * s, 6 * s, 6 * s, c);
    } else if (k == 1) {
        Outline(x, y + 2 * s, 18 * s, 14 * s, c);
        fb.Rect(x + 3 * s, y + 5 * s, 4 * s, 4 * s, c);
        fb.Rect(x + 9 * s, y + 5 * s, 6 * s, s, c);
        fb.Rect(x + 9 * s, y + 8 * s, 5 * s, s, c);
        fb.Rect(x + 3 * s, y + 12 * s, 12 * s, s, c);
    } else if (k == 2) {
        Outline(x, y, 18 * s, 14 * s, c);
        fb.Rect(x + 8 * s, y + 14 * s, 2 * s, 3 * s, c);
        fb.Rect(x + 4 * s, y + 17 * s, 10 * s, s, c);
        for (uint32_t i = 0; i < 4; ++i)
            fb.Rect(x + (3 + i * 3) * s, y + (9 - i * 2) * s, 2 * s, (3 + i * 2) * s, c);
    } else if (k == 3) {
        Outline(x + 3 * s, y + 3 * s, 12 * s, 12 * s, c);
        Outline(x + 7 * s, y + 7 * s, 4 * s, 4 * s, c);
        fb.Rect(x + 7 * s, y, 4 * s, 3 * s, c);
        fb.Rect(x + 7 * s, y + 15 * s, 4 * s, 3 * s, c);
        fb.Rect(x, y + 7 * s, 3 * s, 4 * s, c);
        fb.Rect(x + 15 * s, y + 7 * s, 3 * s, 4 * s, c);
    } else {
        Rounded(x, y + 3 * s, 18 * s, 12 * s, 3 * s, c);
        fb.Rect(x + 3 * s, y + 8 * s, 6 * s, s, 0x142534);
        fb.Rect(x + 5 * s, y + 6 * s, s, 5 * s, 0x142534);
        fb.Rect(x + 13 * s, y + 7 * s, 2 * s, 2 * s, 0x142534);
    }
}
const uint8_t *gtos::gui::ModernGameGlyph(char c) {
    if (c >= 'a' && c <= 'z')
        c -= 32;
    for (uint32_t i = 0; i < sizeof(font) / sizeof(font[0]); ++i)
        if (font[i].c == c)
            return font[i].rows;
    return 0;
}
