#include <drivers/keymap.h>
using namespace gtos::drivers;
Set1Keymap::Set1Keymap()
    : extended(false), leftShift(false), rightShift(false), capsLock(false), capsDown(false),
      pauseBytes(0) {
    for (uint32_t i = 0; i < 256; ++i)
        pressed[i] = 0;
}
bool Set1Keymap::Feed(uint8_t raw, uint8_t &key, bool &down) {
    key = 0;
    down = false;
    if (pauseBytes) {
        --pauseBytes;
        return false;
    }
    if (raw == 0xE1) {
        pauseBytes = 5;
        extended = false;
        return false;
    }
    if (raw == 0xE0) {
        extended = true;
        return false;
    }
    if (raw == 0xFA || raw == 0xFE || raw == 0) {
        extended = false;
        return false;
    }
    bool release = raw & 0x80;
    uint8_t scan = raw & 0x7F;
    uint32_t slot = scan + (extended ? 128 : 0);
    bool wasExtended = extended;
    extended = false;
    if (scan == 0x2A && !wasExtended) {
        leftShift = !release;
        return false;
    }
    if (scan == 0x36 && !wasExtended) {
        rightShift = !release;
        return false;
    }
    if (scan == 0x3A && !wasExtended) {
        if (!release && !capsDown)
            capsLock = !capsLock;
        capsDown = !release;
        return false;
    }
    if (release) {
        key = pressed[slot];
        pressed[slot] = 0;
        return key != 0;
    }
    down = true;
    if (pressed[slot]) {
        key = pressed[slot];
        return true;
    }
    static const char normal[] =
        "\0\0331234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
    static const char shifted[] =
        "\0\033!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";
    if (scan == 0x48)
        key = 0x83;
    else if (scan == 0x50)
        key = 0x84;
    else if (scan == 0x4B)
        key = 0x81;
    else if (scan == 0x4D)
        key = 0x82;
    else if (!wasExtended && scan < sizeof(normal) - 1) {
        key = (leftShift || rightShift) ? shifted[scan] : normal[scan];
        if (capsLock && key >= 'a' && key <= 'z')
            key -= 32;
        else if (capsLock && key >= 'A' && key <= 'Z')
            key += 32;
    } else if (wasExtended && scan == 0x1C)
        key = '\n';
    pressed[slot] = key;
    return key != 0;
}
