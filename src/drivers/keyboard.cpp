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
    uint8_t key;
    bool down;
    if (keymap.Feed(dataport.Read(), key, down) && handler) {
        if (down)
            handler->OnKeyDown((char)key);
        else
            handler->OnKeyUp((char)key);
    }
    return esp;
}
