#include <drivers/framebuffer.h>
using namespace gtos::drivers;
Framebuffer::Framebuffer()
    : video(0), pixels(0), clipX(0), clipY(0), clipR(0), clipB(0), dirtyX(0), dirtyY(0), dirtyR(0),
      dirtyB(0), ready(false), nativeRGB(false) {}
bool Framebuffer::Validate(const FramebufferMode &m) {
    if (!m.width || !m.height || m.width > 1920 || m.height > 1080 || m.pitch < m.width * 4 ||
        m.pitch > 16384 || (m.pitch & 3))
        return false;
    const uint8_t p[] = {m.redPosition, m.greenPosition, m.bluePosition},
                  s[] = {m.redSize, m.greenSize, m.blueSize};
    uint32_t all = 0;
    for (uint32_t i = 0; i < 3; ++i) {
        if (!s[i] || s[i] > 8 || p[i] > 31 || p[i] + s[i] > 32)
            return false;
        uint32_t mask = ((1u << s[i]) - 1) << p[i];
        if (mask & all)
            return false;
        all |= mask;
    }
    return true;
}
bool Framebuffer::ReadMode(const gtos::memory::MultibootInfo *info, FramebufferMode &m) {
    if (!info || !(info->flags & (1u << 12)) || info->framebufferType != 1 ||
        info->framebufferBitsPerPixel != 32 || !info->framebufferAddress)
        return false;
    m.width = info->framebufferWidth;
    m.height = info->framebufferHeight;
    m.pitch = info->framebufferPitch;
    m.redPosition = info->framebufferColorInfo[0];
    m.redSize = info->framebufferColorInfo[1];
    m.greenPosition = info->framebufferColorInfo[2];
    m.greenSize = info->framebufferColorInfo[3];
    m.bluePosition = info->framebufferColorInfo[4];
    m.blueSize = info->framebufferColorInfo[5];
    if (!Validate(m) || info->framebufferAddress > 0xFFFFFFFFu || (info->framebufferAddress & 3))
        return false;
    return info->framebufferAddress + (uint64_t)m.pitch * m.height <= 0x100000000ULL;
}
bool Framebuffer::Configure(const gtos::memory::MultibootInfo *info, uint32_t *backing,
                            uint32_t capacity) {
    FramebufferMode m;
    if (!ReadMode(info, m)) {
        ready = false;
        return false;
    }
    return Bind(m, (volatile uint8_t *)(unsigned long)info->framebufferAddress, backing, capacity);
}
bool Framebuffer::Bind(const FramebufferMode &m, volatile uint8_t *target, uint32_t *backing,
                       uint32_t capacity) {
    ready = false;
    if (!Validate(m) || !target || !backing || capacity < m.width * m.height ||
        ((unsigned long)target & 3) || ((unsigned long)backing & 3))
        return false;
    unsigned long t = (unsigned long)target, b = (unsigned long)backing;
    unsigned long te = t + (unsigned long)m.pitch * m.height,
                  be = b + (unsigned long)m.width * m.height * 4;
    if (te < t || be < b || (t < be && b < te))
        return false;
    mode = m;
    video = target;
    pixels = backing;
    ready = true;
    nativeRGB = m.redPosition == 16 && m.redSize == 8 && m.greenPosition == 8 && m.greenSize == 8 &&
                m.bluePosition == 0 && m.blueSize == 8;
    ResetClip();
    dirtyX = mode.width;
    dirtyY = mode.height;
    dirtyR = dirtyB = 0;
    Clear(0);
    return true;
}
void Framebuffer::Damage(int32_t x, int32_t y, int32_t r, int32_t b) {
    if (x < dirtyX)
        dirtyX = x;
    if (y < dirtyY)
        dirtyY = y;
    if (r > dirtyR)
        dirtyR = r;
    if (b > dirtyB)
        dirtyB = b;
}
void Framebuffer::ResetClip() {
    clipX = clipY = 0;
    clipR = Width();
    clipB = Height();
}
void Framebuffer::SetClip(int32_t x, int32_t y, int32_t w, int32_t h) {
    int64_t r = (int64_t)x + w, b = (int64_t)y + h;
    clipX = x < 0 ? 0 : x;
    clipY = y < 0 ? 0 : y;
    clipR = r > (int64_t)Width() ? Width() : (r < 0 ? 0 : (int32_t)r);
    clipB = b > (int64_t)Height() ? Height() : (b < 0 ? 0 : (int32_t)b);
    if (w <= 0 || h <= 0 || clipX >= clipR || clipY >= clipB)
        clipX = clipY = clipR = clipB = 0;
}
void Framebuffer::Pixel(int32_t x, int32_t y, uint32_t c) {
    if (!ready || x < clipX || y < clipY || x >= clipR || y >= clipB)
        return;
    pixels[y * mode.width + x] = c & 0xFFFFFFu;
    Damage(x, y, x + 1, y + 1);
}
uint32_t Framebuffer::ReadPixel(int32_t x, int32_t y) const {
    return ready && x >= 0 && y >= 0 && (uint32_t)x < mode.width && (uint32_t)y < mode.height
               ? pixels[y * mode.width + x]
               : 0;
}
void Framebuffer::Blend(int32_t x, int32_t y, uint32_t c, uint8_t a) {
    if (!a || !ready || x < clipX || y < clipY || x >= clipR || y >= clipB)
        return;
    if (a >= 15) {
        Pixel(x, y, c);
        return;
    }
    uint32_t d = pixels[y * mode.width + x], out = 0;
    for (uint32_t shift = 0; shift <= 16; shift += 8)
        out |= ((((c >> shift) & 255) * a + ((d >> shift) & 255) * (15 - a) + 7) / 15) << shift;
    Pixel(x, y, out);
}
void Framebuffer::Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
    if (!ready || w <= 0 || h <= 0)
        return;
    int64_t rr = (int64_t)x + w, bb = (int64_t)y + h;
    int32_t r = rr > clipR ? clipR : (rr < clipX ? clipX : (int32_t)rr),
            b = bb > clipB ? clipB : (bb < clipY ? clipY : (int32_t)bb);
    if (x < clipX)
        x = clipX;
    if (y < clipY)
        y = clipY;
    if (x >= r || y >= b)
        return;
    c &= 0xFFFFFFu;
    for (int32_t j = y; j < b; ++j) {
        uint32_t *row = pixels + j * mode.width;
        for (int32_t i = x; i < r; ++i)
            row[i] = c;
    }
    Damage(x, y, r, b);
}
void Framebuffer::Clear(uint32_t c) {
    Rect(0, 0, Width(), Height(), c);
}

void Framebuffer::BlitRgba(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t* source,
                           uint32_t sourceWidth, uint32_t sourceHeight) {
    if (!ready || !source || w <= 0 || h <= 0 || w > 1920 || h > 1080 ||
        !sourceWidth || !sourceHeight ||
        sourceWidth > 1920 || sourceHeight > 1080)
        return;
    const int64_t right = (int64_t)x + w, bottom = (int64_t)y + h;
    const int32_t left = x > clipX ? x : clipX, top = y > clipY ? y : clipY;
    const int32_t endX = right < clipR ? (int32_t)right : clipR;
    const int32_t endY = bottom < clipB ? (int32_t)bottom : clipB;
    if (left >= endX || top >= endY) return;
    // The horizontal sampling positions are the same for every row.
    // The width limits above bound both the number and size of the offsets.
    uint16_t horizontalOffsets[1920];
    for (int32_t column = left; column < endX; ++column) {
        const uint32_t sourceX = (uint32_t)(column - x) * sourceWidth / (uint32_t)w;
        horizontalOffsets[column - left] = sourceX * 4;
    }
    for (int32_t row = top; row < endY; ++row) {
        const uint32_t sourceY = (uint32_t)(row - y) * sourceHeight / (uint32_t)h;
        const uint8_t* sourceRow = source + sourceY * sourceWidth * 4;
        uint32_t* output = pixels + row * mode.width;
        const uint16_t* horizontal = horizontalOffsets;
        for (int32_t column = left; column < endX; ++column) {
            const uint8_t* rgba = sourceRow + *horizontal++;
            uint32_t color = (uint32_t)rgba[0] << 16 | (uint32_t)rgba[1] << 8 | rgba[2];
            if (rgba[3] != 255) {
                const uint32_t background = output[column], inverseAlpha = 255 - rgba[3];
                color = 0;
                for (uint32_t channel = 0; channel < 3; ++channel) {
                    const uint32_t shift = 16 - channel * 8;
                    const uint32_t value = rgba[channel] +
                        (((background >> shift) & 255) * inverseAlpha + 127) / 255;
                    color |= value << shift;
                }
            }
            output[column] = color;
        }
    }
    Damage(left, top, endX, endY);
}
void Framebuffer::Present() {
    if (!ready || dirtyX >= dirtyR || dirtyY >= dirtyB)
        return;
    for (int32_t y = dirtyY; y < dirtyB; ++y) {
        volatile uint32_t *dst = (volatile uint32_t *)(video + y * mode.pitch) + dirtyX;
        const uint32_t *src = pixels + y * mode.width + dirtyX;
        if (nativeRGB) {
            uint32_t words = dirtyR - dirtyX;
            asm volatile("cld; rep movsl" : "+D"(dst), "+S"(src), "+c"(words) : : "memory", "cc");
            continue;
        }
        for (int32_t x = dirtyX; x < dirtyR; ++x) {
            const uint32_t c = *src++;
            *dst++ = ((((c >> 16) & 255) >> (8 - mode.redSize)) << mode.redPosition) |
                     ((((c >> 8) & 255) >> (8 - mode.greenSize)) << mode.greenPosition) |
                     (((c & 255) >> (8 - mode.blueSize)) << mode.bluePosition);
        }
    }
    dirtyX = mode.width;
    dirtyY = mode.height;
    dirtyR = dirtyB = 0;
}
