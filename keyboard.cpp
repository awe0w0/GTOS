#include "keyboard.h"

KeyboardDriver::KeyboardDriver(InterruptsManager* manager) 
:InterruptHandler(0x21,manager),
dataport(0x60),
commandport(0x64) {
    while (commandport.Read() && 0x1)
        dataport.Read();
    commandport.Write(0xAE); //激活中断
    commandport.Write(0x20); //获得目前状态
    uint8_t status = (dataport.Read() | 1) & ~0x10;
    commandport.Write(0x60); //设置状态
    dataport.Write(status);

    dataport.Write(0xf4);
}

KeyboardDriver::~KeyboardDriver() {

}

void printf();

uint32_t KeyboardDriver::HandlerInterrupt(uint32_t esp) {
    uint8_t key = dataport.Read();

    char* foo = "KEYBOARD 0X00";
    char* hex = "0123456789ABCDEF";
    foo[11] = hex[(key >> 4) & 0x0f];
    foo[12] = hex[key & 0x0f];

    return esp;
}