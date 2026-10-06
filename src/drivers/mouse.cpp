#include <drivers/mouse.h>

using namespace gtos::drivers;
using namespace gtos::hardwarecommunication;

MouseEventHandler::MouseEventHandler() {}

void MouseEventHandler::OnActivate() {}

void MouseEventHandler::OnMouseDown(uint8_t button) {}

void MouseEventHandler::OnMouseUp(uint8_t button) {}

void MouseEventHandler::OnMouseMove(int8_t x, int8_t y) {}

gtos::drivers::MouseDriver::MouseDriver(InterruptsManager *manager, MouseEventHandler *handler)
    : InterruptHandler(manager, 0x2C), dataport(0x60), commandport(0x64) {

    this->handler = handler;
}

gtos::drivers::MouseDriver::~MouseDriver() {}

void gtos::drivers::MouseDriver::Activate() {
    offset = 0;
    buttons = 0;
    for (uint32_t n = 0; n < 100000 && (commandport.Read() & 2); ++n) {
    }
    commandport.Write(0xA8);
    for (uint32_t n = 0; n < 100000 && (commandport.Read() & 2); ++n) {
    }
    commandport.Write(0x20);
    for (uint32_t n = 0; n < 100000 && !(commandport.Read() & 1); ++n) {
    }
    uint8_t status = dataport.Read() | 2;
    commandport.Write(0x60);
    for (uint32_t n = 0; n < 100000 && (commandport.Read() & 2); ++n) {
    }
    dataport.Write(status);
    for (uint32_t n = 0; n < 100000 && (commandport.Read() & 2); ++n) {
    }
    commandport.Write(0xD4);
    for (uint32_t n = 0; n < 100000 && (commandport.Read() & 2); ++n) {
    }
    dataport.Write(0xF4);
    for (uint32_t n = 0; n < 100000 && !(commandport.Read() & 1); ++n) {
    }
    if (commandport.Read() & 1)
        dataport.Read();
    if (handler)
        handler->OnActivate();
}
void printf(char *);

// 在InterruptsManager的handlers数组中挂载后由中断调用
uint32_t gtos::drivers::MouseDriver::HandlerInterrupt(uint32_t esp) {
    uint8_t status = commandport.Read();
    if ((!(status & 0x20)) || handler == 0) {
        return esp;
    }

    uint8_t value = dataport.Read();
    if (offset == 0 && !(value & 0x08))
        return esp;
    buffer[offset] = value;
    offset = (offset + 1) % 3;

    if (offset == 0) {
        if (!(buffer[0] & 0xC0) && (buffer[1] != 0 || buffer[2] != 0)) {
            int32_t dx = (int32_t)buffer[1] - ((buffer[0] & 0x10) ? 256 : 0);
            int32_t dy = -((int32_t)buffer[2] - ((buffer[0] & 0x20) ? 256 : 0));
            if (dx > 127)
                dx = 127;
            if (dx < -127)
                dx = -127;
            if (dy > 127)
                dy = 127;
            if (dy < -127)
                dy = -127;
            handler->OnMouseMove((int8_t)dx, (int8_t)dy);
        }
        for (uint8_t i = 0; i < 3; i++) {
            if ((buffer[0] & (0x01 << i)) != (buttons & (0x01 << i))) {
                if (buttons & (0x01 << i)) {
                    handler->OnMouseUp(i + 1);
                } else {
                    handler->OnMouseDown(i + 1);
                }
            }
        }
        buttons = buffer[0];
    }

    return esp;
}
