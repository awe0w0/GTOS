#ifndef __GTOS__DRIVERS__KEYBOARD_H
#define __GTOS__DRIVERS__KEYBOARD_H

#include <common/types.h>
#include <hardwarecommunication/interrupts.h>
#include <hardwarecommunication/port.h>
#include <drivers/driver.h>

namespace gtos {
    namespace drivers {

        class KeyboardEventHandler {
            public:
                KeyboardEventHandler();

                virtual void OnKeyDown(char);
                virtual void OnKeyUp(char);
        };

        class KeyboardDriver : public gtos::hardwarecommunication::InterruptHandler , public Driver {
            gtos::hardwarecommunication::Port8Bit dataport;
            gtos::hardwarecommunication::Port8Bit commandport;

            KeyboardEventHandler* handler;
        public:
            KeyboardDriver(gtos::hardwarecommunication::InterruptsManager* manager, KeyboardEventHandler* handler );
            ~KeyboardDriver();
            virtual uint32_t HandlerInterrupt(uint32_t esp);
            virtual void Activate();
        };
    }
}

#endif