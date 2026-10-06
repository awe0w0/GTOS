#ifndef GTOS_I18N_FONT_H
#define GTOS_I18N_FONT_H
#include <i18n/utf8.h>
namespace gtos {
namespace i18n {
struct Glyph {
    const uint8_t *pixels; // Row-major, high nibble first, 4-bit alpha.
    uint16_t width;
    uint16_t height;
    uint16_t advance;
};
// Chinese/punctuation/replacement atlas only. scale 1 or 2; caller zooms scale 1 for 3.
// Missing codepoints return false and leave result unchanged; use U+FFFD visibly.
bool LookupGlyph(uint32_t codepoint, uint32_t scale, Glyph &result);
// Printable ASCII is covered by ModernPainter's existing DejaVu atlas.
bool HasGlyph(uint32_t codepoint);
bool CoversText(const char *text, uint32_t maxBytes = MaxTextBytes);
uint32_t GlyphCount();
uint32_t AtlasBytes();
uint32_t GlyphCodepoint(uint32_t index);
} // namespace i18n
} // namespace gtos
#endif
