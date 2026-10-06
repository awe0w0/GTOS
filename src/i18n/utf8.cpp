#include <i18n/utf8.h>
using namespace gtos::i18n;
DecodeResult gtos::i18n::Decode(const char *text, uint32_t available) {
    DecodeResult result = {0xFFFD, 0, false};
    if (!text || !available)
        return result;
    uint8_t lead = (uint8_t)text[0];
    if (!lead) {
        result.codepoint = 0;
        result.valid = true;
        return result;
    }
    result.bytes = 1;
    if (lead < 0x80) {
        result.codepoint = lead;
        result.valid = true;
        return result;
    }
    uint32_t n, cp, minimum;
    if (lead >= 0xC2 && lead <= 0xDF) {
        n = 2;
        cp = lead & 0x1F;
        minimum = 0x80;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        n = 3;
        cp = lead & 0x0F;
        minimum = 0x800;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        n = 4;
        cp = lead & 7;
        minimum = 0x10000;
    } else
        return result;
    if (available < n)
        return result;
    for (uint32_t i = 1; i < n; ++i) {
        uint8_t b = (uint8_t)text[i];
        if ((b & 0xC0) != 0x80)
            return result;
        cp = (cp << 6) | (b & 0x3F);
    }
    if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        return result;
    result.codepoint = cp;
    result.bytes = n;
    result.valid = true;
    return result;
}
uint32_t gtos::i18n::ByteLength(const char *text, uint32_t maxBytes) {
    if (!text)
        return 0;
    uint32_t n = 0;
    while (n < maxBytes && text[n])
        ++n;
    return n;
}
bool gtos::i18n::Validate(const char *text, uint32_t maxBytes) {
    if (!text)
        return false;
    uint32_t i = 0;
    while (i < maxBytes) {
        if (!text[i])
            return true;
        DecodeResult cp = Decode(text + i, maxBytes - i);
        if (!cp.valid || !cp.bytes)
            return false;
        i += cp.bytes;
    }
    return false;
}
uint32_t gtos::i18n::CodepointCount(const char *text, uint32_t maxBytes) {
    if (!text)
        return 0;
    uint32_t i = 0, count = 0;
    while (i < maxBytes && text[i]) {
        DecodeResult cp = Decode(text + i, maxBytes - i);
        if (!cp.valid || !cp.bytes)
            break;
        i += cp.bytes;
        ++count;
    }
    return count;
}
uint32_t gtos::i18n::Copy(char *dst, uint32_t capacity, const char *src, uint32_t maxCodepoints,
                          uint32_t srcBytes) {
    if (!dst || !capacity)
        return 0;
    uint32_t bytes = 0, count = 0;
    if (src)
        while (bytes < srcBytes && src[bytes] && count < maxCodepoints) {
            DecodeResult cp = Decode(src + bytes, srcBytes - bytes);
            if (!cp.valid || !cp.bytes || cp.bytes >= capacity - bytes)
                break;
            for (uint32_t j = 0; j < cp.bytes; ++j)
                dst[bytes + j] = src[bytes + j];
            bytes += cp.bytes;
            ++count;
        }
    dst[bytes] = 0;
    return bytes;
}
bool gtos::i18n::Append(char *dst, uint32_t capacity, const char *src, uint32_t srcBytes) {
    if (!dst || !capacity || !Validate(dst, capacity) || !Validate(src, srcBytes))
        return false;
    uint32_t used = ByteLength(dst, capacity), add = ByteLength(src, srcBytes);
    if (add >= capacity - used)
        return false;
    // Copy backwards so appending the same buffer (or its suffix) remains safe.
    for (uint32_t i = add; i > 0; --i)
        dst[used + i - 1] = src[i - 1];
    dst[used + add] = 0;
    return true;
}
bool gtos::i18n::Backspace(char *text, uint32_t capacity) {
    if (!Validate(text, capacity))
        return false;
    uint32_t pos = 0, last = 0;
    while (text[pos]) {
        last = pos;
        pos += Decode(text + pos, capacity - pos).bytes;
    }
    if (!pos)
        return false;
    text[last] = 0;
    return true;
}
namespace {
uint32_t Fold(uint32_t cp) {
    return cp >= 'A' && cp <= 'Z' ? cp + ('a' - 'A') : cp;
}
} // namespace
bool gtos::i18n::ContainsAsciiFold(const char *text, const char *query, uint32_t maxBytes) {
    if (!Validate(text, maxBytes) || !Validate(query, maxBytes))
        return false;
    if (!query[0])
        return true;
    for (uint32_t start = 0; text[start]; start += Decode(text + start, maxBytes - start).bytes) {
        uint32_t a = start, b = 0;
        while (text[a] && query[b]) {
            DecodeResult ca = Decode(text + a, maxBytes - a), cb = Decode(query + b, maxBytes - b);
            if (Fold(ca.codepoint) != Fold(cb.codepoint))
                break;
            a += ca.bytes;
            b += cb.bytes;
        }
        if (!query[b])
            return true;
    }
    return false;
}
