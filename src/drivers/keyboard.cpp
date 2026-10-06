#include <drivers/keyboard.h>
using namespace gtos::hardwarecommunication;
using namespace gtos::drivers;
KeyboardEventHandler::KeyboardEventHandler() {}
void KeyboardEventHandler::OnKeyDown(char) {}
void KeyboardEventHandler::OnKeyUp(char) {}
KeyboardDriver::KeyboardDriver(InterruptsManager *manager, KeyboardEventHandler *h)
    : InterruptHandler(manager, 0x21), dataport(0x60), commandport(0x64), handler(h) {}
KeyboardDriver::~KeyboardDriver() {}
void KeyboardDriver::Activate() {
    for (uint32_t n = 0; n < 100000 && (commandport.Read() & 1); ++n)
        dataport.Read();
    for (uint32_t n = 0; n < 100000 && (commandport.Read() & 2); ++n) {
    }
    commandport.Write(0xAE);
    for (uint32_t n = 0; n < 100000 && (commandport.Read() & 2); ++n) {
    }
    commandport.Write(0x20);
    for (uint32_t n = 0; n < 100000 && !(commandport.Read() & 1); ++n) {
    }
    uint8_t status = (dataport.Read() | 1) & ~0x10;
    commandport.Write(0x60);
    for (uint32_t n = 0; n < 100000 && (commandport.Read() & 2); ++n) {
    }
    dataport.Write(status);
    for (uint32_t n = 0; n < 100000 && (commandport.Read() & 2); ++n) {
    }
    dataport.Write(0xF4);
    for (uint32_t n = 0; n < 100000 && !(commandport.Read() & 1); ++n) {
    }
    if (commandport.Read() & 1)
        dataport.Read();
}
uint32_t KeyboardDriver::HandlerInterrupt(uint32_t esp) {
    const uint8_t raw = dataport.Read();
    static bool extended = false, leftShift = false, rightShift = false, caps = false;
    if (raw == 0xE0) {
        extended = true;
        return esp;
    }
    if (raw == 0xFA || raw == 0xFE)
        return esp;
    bool release = raw & 0x80;
    uint8_t scan = raw & 0x7F;
    uint8_t key = 0;
    if (scan == 0x2A && !extended) {
        leftShift = !release;
        return esp;
    }
    if (scan == 0x36 && !extended) {
        rightShift = !release;
        return esp;
    }
    if (scan == 0x3A && !release && !extended) {
        caps = !caps;
        return esp;
    }
    bool shifted = leftShift || rightShift;
    static const char normal[] =
        "\0\0331234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
    static const char upper[] =
        "\0\033!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";
    if (scan == 0x48)
        key = 0x83;
    else if (scan == 0x50)
        key = 0x84;
    else if (scan == 0x4B)
        key = 0x81;
    else if (scan == 0x4D)
        key = 0x82;
    else if (scan < sizeof(normal) - 1) {
        key = shifted ? upper[scan] : normal[scan];
        if (caps && key >= 'a' && key <= 'z')
            key -= 32;
        else if (caps && key >= 'A' && key <= 'Z')
            key += 32;
    }
    extended = false;
    if (handler && key) {
        if (release)
            handler->OnKeyUp((char)key);
        else
            handler->OnKeyDown((char)key);
    }
    return esp;
}
