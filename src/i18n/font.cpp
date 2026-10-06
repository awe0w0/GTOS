#include <i18n/font.h>
#include <i18n/han_font_data.inc>
using namespace gtos::i18n;
uint32_t gtos::i18n::GlyphCount() {
    return sizeof(hanCodepoints) / sizeof(hanCodepoints[0]);
}
uint32_t gtos::i18n::AtlasBytes() {
    return sizeof(hanCodepoints) + sizeof(hanAdvance1) + sizeof(hanAdvance2) + sizeof(hanPixels1) +
           sizeof(hanPixels2);
}
uint32_t gtos::i18n::GlyphCodepoint(uint32_t index) {
    return index < GlyphCount() ? hanCodepoints[index] : 0;
}
bool gtos::i18n::LookupGlyph(uint32_t codepoint, uint32_t scale, Glyph &result) {
    if (scale != 1 && scale != 2)
        return false;
    // Noto CJK has no U+FFFD: use its licensed U+25A1 visible replacement square.
    if (codepoint == 0xFFFD)
        codepoint = 0x25A1;
    uint32_t low = 0, high = GlyphCount();
    while (low < high) {
        uint32_t mid = low + (high - low) / 2;
        if (hanCodepoints[mid] < codepoint)
            low = mid + 1;
        else
            high = mid;
    }
    if (low == GlyphCount() || hanCodepoints[low] != codepoint)
        return false;
    result.pixels = scale == 1 ? hanPixels1[low] : hanPixels2[low];
    result.width = 16 * scale;
    result.height = 18 * scale;
    result.advance = scale == 1 ? hanAdvance1[low] : hanAdvance2[low];
    return true;
}
bool gtos::i18n::HasGlyph(uint32_t cp) {
    if (cp >= 32 && cp <= 126)
        return true;
    Glyph glyph;
    return LookupGlyph(cp, 1, glyph);
}
bool gtos::i18n::CoversText(const char *text, uint32_t maxBytes) {
    if (!Validate(text, maxBytes))
        return false;
    for (uint32_t i = 0; text[i];) {
        DecodeResult cp = Decode(text + i, maxBytes - i);
        if (cp.codepoint != '\n' && !HasGlyph(cp.codepoint))
            return false;
        i += cp.bytes;
    }
    return true;
}
