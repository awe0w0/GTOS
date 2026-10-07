// Complete byte operations used by actual target compiler lowering. These
// execute on the user's real memory; they are not successful-return stubs.
#include <stddef.h>
#include <string.h>
extern "C" void* memset(void* destination, int value, size_t bytes) noexcept {
    volatile unsigned char* output = (volatile unsigned char*)destination;
    for (size_t i = 0; i < bytes; ++i) output[i] = (unsigned char)value;
    return destination;
}
extern "C" void* memcpy(void* destination, const void* source, size_t bytes) noexcept {
    volatile unsigned char* output = (volatile unsigned char*)destination;
    const volatile unsigned char* input = (const volatile unsigned char*)source;
    for (size_t i = 0; i < bytes; ++i) output[i] = input[i];
    return destination;
}
extern "C" void* memmove(void* destination, const void* source, size_t bytes) noexcept {
    volatile unsigned char* output = (volatile unsigned char*)destination;
    const volatile unsigned char* input = (const volatile unsigned char*)source;
    if ((unsigned)destination <= (unsigned)source) {
        for (size_t i = 0; i < bytes; ++i) output[i] = input[i];
    } else {
        for (size_t i = bytes; i; --i) output[i - 1] = input[i - 1];
    }
    return destination;
}
