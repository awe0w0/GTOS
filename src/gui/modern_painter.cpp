#include "modern_font_data.inc"
#include "modern_game_font.inc"
#include <gui/modern_painter.h>
#include <i18n/font.h>
#include <i18n/utf8.h>
using namespace gtos::gui;
namespace {
struct TextGlyph {
    const uint8_t *pixels;
    uint32_t width, height, advance, zoom;
};
TextGlyph SelectTextGlyph(uint32_t codepoint, uint32_t scale) {
    TextGlyph out = {};
    uint32_t raster = scale == 2 ? 2 : 1;
    out.zoom = scale / raster;
    if (codepoint >= 32 && codepoint <= 126) {
        uint32_t index = codepoint - 32;
        out.pixels = raster == 2 ? modernFontPixelsLarge[index] : modernFontPixels[index];
        out.width = 16 * raster;
        out.height = 18 * raster;
        out.advance = raster == 2 ? modernFontAdvanceLarge[index] : modernFontAdvance[index];
    } else {
        gtos::i18n::Glyph glyph = {};
        if (!gtos::i18n::LookupGlyph(codepoint, raster, glyph))
            gtos::i18n::LookupGlyph(0xFFFD, raster, glyph);
        out.pixels = glyph.pixels;
        out.width = glyph.width;
        out.height = glyph.height;
        out.advance = glyph.advance;
    }
    return out;
}
} // namespace
void ModernPainter::Text(int32_t x, int32_t y, const char *text, uint32_t color, uint32_t scale) {
    if (!text || scale < 1 || scale > 3)
        return;
    int64_t px = x, py = y;
    uint32_t offset = 0;
    for (uint32_t count = 0; count < 160 && offset < gtos::i18n::MaxTextBytes && text[offset];
         ++count) {
        gtos::i18n::DecodeResult cp =
            gtos::i18n::Decode(text + offset, gtos::i18n::MaxTextBytes - offset);
        if (!cp.bytes)
            break;
        offset += cp.bytes;
        if (cp.codepoint == '\n') {
            px = x;
            py += 20 * scale;
            continue;
        }
        TextGlyph glyph = SelectTextGlyph(cp.codepoint, scale);
        if (px >= -(int64_t)glyph.width * glyph.zoom && px < (int64_t)fb.Width() &&
            py >= -(int64_t)glyph.height * glyph.zoom && py < (int64_t)fb.Height()) {
            for (uint32_t row = 0; row < glyph.height; ++row) {
                for (uint32_t col = 0; col < glyph.width; ++col) {
                    uint32_t at = row * glyph.width + col;
                    uint8_t alpha = at & 1 ? glyph.pixels[at / 2] & 15 : glyph.pixels[at / 2] >> 4;
                    if (!alpha)
                        continue;
                    for (uint32_t sy = 0; sy < glyph.zoom; ++sy)
                        for (uint32_t sx = 0; sx < glyph.zoom; ++sx)
                            fb.Blend((int32_t)px + col * glyph.zoom + sx,
                                     (int32_t)py + row * glyph.zoom + sy, color, alpha);
                }
            }
        }
        px += glyph.advance * glyph.zoom;
    }
}
int32_t ModernPainter::TextWidth(const char *text, uint32_t scale) const {
    if (!text || scale < 1 || scale > 3)
        return 0;
    uint32_t offset = 0;
    int32_t width = 0;
    for (uint32_t count = 0; count < 160 && offset < gtos::i18n::MaxTextBytes && text[offset];
         ++count) {
        gtos::i18n::DecodeResult cp =
            gtos::i18n::Decode(text + offset, gtos::i18n::MaxTextBytes - offset);
        if (!cp.bytes || cp.codepoint == '\n')
            break;
        offset += cp.bytes;
        TextGlyph glyph = SelectTextGlyph(cp.codepoint, scale);
        width += glyph.advance * glyph.zoom;
    }
    return width;
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
