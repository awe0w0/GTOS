#include <gui/canvas.h>
using namespace gtos::gui;
// Original compact 5x7 uppercase font, one low-five-bit row per byte.
struct Glyph {
    char c;
    uint8_t rows[7];
};
static const Glyph font[] = {
    {'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'C', {14, 17, 16, 16, 16, 17, 14}}, {'D', {30, 17, 17, 17, 17, 17, 30}},
    {'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {14, 17, 16, 23, 17, 17, 15}}, {'H', {17, 17, 17, 31, 17, 17, 17}},
    {'I', {14, 4, 4, 4, 4, 4, 14}},      {'J', {7, 2, 2, 2, 18, 18, 12}},
    {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 25, 21, 19, 17, 17, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}}, {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},   {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'U', {17, 17, 17, 17, 17, 17, 14}}, {'V', {17, 17, 17, 17, 17, 10, 4}},
    {'W', {17, 17, 17, 21, 21, 21, 10}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 10, 4, 4, 4, 4}},     {'Z', {31, 1, 2, 4, 8, 16, 31}},
    {'0', {14, 17, 19, 21, 25, 17, 14}}, {'1', {4, 12, 4, 4, 4, 4, 14}},
    {'2', {14, 17, 1, 2, 4, 8, 31}},     {'3', {30, 1, 1, 14, 1, 1, 30}},
    {'4', {2, 6, 10, 18, 31, 2, 2}},     {'5', {31, 16, 16, 30, 1, 1, 30}},
    {'6', {14, 16, 16, 30, 17, 17, 14}}, {'7', {31, 1, 2, 4, 8, 8, 8}},
    {'8', {14, 17, 17, 14, 17, 17, 14}}, {'9', {14, 17, 17, 15, 1, 1, 14}},
    {'.', {0, 0, 0, 0, 0, 12, 12}},      {':', {0, 12, 12, 0, 12, 12, 0}},
    {'/', {1, 2, 2, 4, 8, 8, 16}},       {'-', {0, 0, 0, 31, 0, 0, 0}},
    {'+', {0, 4, 4, 31, 4, 4, 0}},       {'=', {0, 0, 31, 0, 31, 0, 0}},
    {'[', {14, 8, 8, 8, 8, 8, 14}},      {']', {14, 2, 2, 2, 2, 2, 14}},
    {'(', {2, 4, 8, 8, 8, 4, 2}},        {')', {8, 4, 2, 2, 2, 4, 8}},
    {'>', {16, 8, 4, 2, 4, 8, 16}},      {'<', {1, 2, 4, 8, 4, 2, 1}},
    {'!', {4, 4, 4, 4, 4, 0, 4}},        {'?', {14, 17, 1, 2, 4, 0, 4}},
    {'%', {17, 2, 4, 4, 8, 16, 17}},     {'_', {0, 0, 0, 0, 0, 0, 31}}};
void Canvas::Clear(uint8_t c) {
    for (uint32_t i = 0; i < sizeof(pixels); ++i)
        pixels[i] = c;
}
void Canvas::Pixel(int32_t x, int32_t y, uint8_t c) {
    if (x >= 0 && x < 320 && y >= 0 && y < 200)
        pixels[y * 320 + x] = c & 15;
}
void Canvas::Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t c) {
    if (w <= 0 || h <= 0)
        return;
    int32_t x2 = x + w, y2 = y + h;
    if (x2 > 320)
        x2 = 320;
    if (y2 > 200)
        y2 = 200;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    for (int32_t j = y; j < y2; ++j)
        for (int32_t i = x; i < x2; ++i)
            pixels[j * 320 + i] = c & 15;
}
void Canvas::Frame(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t c) {
    Rect(x, y, w, 1, c);
    Rect(x, y + h - 1, w, 1, c);
    Rect(x, y, 1, h, c);
    Rect(x + w - 1, y, 1, h, c);
}
void Canvas::Text(int32_t x, int32_t y, const char *t, uint8_t c, uint32_t scale) {
    if (!t || scale < 1 || scale > 3)
        return;
    int32_t start = x;
    for (uint32_t n = 0; t[n] && n < 96; ++n) {
        char ch = t[n];
        if (ch == '\n') {
            y += 9 * scale;
            x = start;
            continue;
        }
        if (ch >= 'a' && ch <= 'z')
            ch -= 32;
        if (ch != ' ')
            for (uint32_t g = 0; g < sizeof(font) / sizeof(font[0]); ++g)
                if (font[g].c == ch) {
                    for (int32_t r = 0; r < 7; ++r)
                        for (int32_t b = 0; b < 5; ++b)
                            if (font[g].rows[r] & (1 << (4 - b)))
                                Rect(x + b * scale, y + r * scale, scale, scale, c);
                    break;
                }
        x += 6 * scale;
        if (x >= 320)
            break;
    }
}
void Canvas::Number(int32_t x, int32_t y, uint32_t v, uint8_t c) {
    char b[11];
    uint32_t n = 0;
    do {
        b[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 10);
    char s[11];
    for (uint32_t i = 0; i < n; ++i)
        s[i] = b[n - 1 - i];
    s[n] = 0;
    Text(x, y, s, c);
}
void Canvas::Present() {
    volatile uint8_t *fb = (volatile uint8_t *)0xA0000;
    for (uint32_t i = 0; i < sizeof(pixels); ++i)
        fb[i] = pixels[i];
}

void Canvas::Blit(const Canvas &source, int32_t sx, int32_t sy, int32_t w, int32_t h, int32_t dx,
                  int32_t dy) {
    for (int32_t y = 0; y < h; ++y)
        for (int32_t x = 0; x < w; ++x)
            if (sx + x >= 0 && sx + x < 320 && sy + y >= 0 && sy + y < 200)
                Pixel(dx + x, dy + y, source.pixels[(sy + y) * 320 + sx + x]);
}
