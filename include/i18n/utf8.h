#ifndef GTOS_I18N_UTF8_H
#define GTOS_I18N_UTF8_H
#include <common/types.h>
namespace gtos {
namespace i18n {
static const uint32_t MaxTextBytes = 4096;
struct DecodeResult {
    uint32_t codepoint;
    uint8_t bytes;
    bool valid;
};
// Invalid input consumes one byte and returns U+FFFD; empty input consumes zero.
DecodeResult Decode(const char *text, uint32_t available);
// Require a NUL terminator inside maxBytes. Reject overlong/surrogate/out-of-range UTF-8.
bool Validate(const char *text, uint32_t maxBytes = MaxTextBytes);
uint32_t ByteLength(const char *text, uint32_t maxBytes = MaxTextBytes);
uint32_t CodepointCount(const char *text, uint32_t maxBytes = MaxTextBytes);
// Copy the valid prefix, never splitting a codepoint; return bytes written, always NUL if cap>0.
uint32_t Copy(char *dst, uint32_t capacity, const char *src, uint32_t maxCodepoints = MaxTextBytes,
              uint32_t srcBytes = MaxTextBytes);
// All-or-nothing validated append. Capacity includes NUL. Source may equal destination.
bool Append(char *dst, uint32_t capacity, const char *src, uint32_t srcBytes = MaxTextBytes);
// Remove exactly one Unicode scalar from valid, bounded NUL-terminated UTF-8.
bool Backspace(char *text, uint32_t capacity);
// Scalar-boundary substring search. Only ASCII A-Z case folding; no Unicode normalization.
bool ContainsAsciiFold(const char *text, const char *query, uint32_t maxBytes = MaxTextBytes);
} // namespace i18n
} // namespace gtos
#endif
