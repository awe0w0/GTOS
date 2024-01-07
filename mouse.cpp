#include "mouse.h"

MouseEventHandler::MouseEventHandler() {

}

void MouseEventHandler::OnActivate() {

}

void MouseEventHandler::OnMouseDown(uint8_t button) {

}

void MouseEventHandler::OnMouseUp(uint8_t button) {

}

void MouseEventHandler::OnMouseMove(int8_t x, int8_t y) {

}

MouseDriver::MouseDriver(InterruptsManager* manager, MouseEventHandler* handler)
: InterruptHandler(0x2C, manager),
dataport(0x60),
commandport(0x64) {
    manager->handlers[0x2C] = this;

    this->handler = handler;

}

MouseDriver::~MouseDriver() {

}

void MouseDriver::Activate() {
    offset = 0;
    buttons = 0;

    commandport.Write(0xA8);//激活中断
    commandport.Write(0x20);//获取目前状态
    uint8_t status = dataport.Read() | 2;
    commandport.Write(0x60);// 设置状态
    dataport.Write(status);

    commandport.Write(0xD4);
    dataport.Write(0xF4);
    dataport.Read();
}
void printf(char*);

uint32_t MouseDriver::HandlerInterrupt(uint32_t esp) {
    uint8_t status = commandport.Read();
    if ((!(status & 0x20)) || handler == 0) {
        return esp;
    }

    buffer[offset] = dataport.Read();
    offset = (offset + 1) % 3;


              
    if (offset == 0) {
        if (buffer[1] != 0 || buffer[2] != 0) {
            uint8_t x = 40, y = 12;
            handler->OnMouseMove(buffer[1], -buffer[2]);
        }        
        for (uint8_t i = 0;i < 3;i++) {
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