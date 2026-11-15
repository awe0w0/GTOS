#include "modern_game_font.inc"

extern "C" const unsigned char* gtos_dashboard_glyph(char character) {
    for (const Glyph& glyph : font)
        if (glyph.c == character) return glyph.rows;
    return nullptr;
}
