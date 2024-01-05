#ifndef __KEYBOARD_H
#define __KEYBOARD_H

#include "types.h"
#include "interrupts.h"
#include "port.h"

class KeyboardDriver : public InterruptHandler {
    Port8Bit dataport;
    Port8Bit commandport;
public:
    KeyboardDriver(InterruptsManager* manager);
    ~KeyboardDriver();
    virtual uint32_t HandlerInterrupt(uint32_t esp);
};

#endif