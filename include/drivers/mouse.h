#ifndef __GTOS__DRIVERS__MOUSE_H
#define __GTOS__DRIVERS__MOUSE_H

#include <common/types.h>
#include <hardwarecommunication/interrupts.h>
#include <hardwarecommunication/port.h>
#include <drivers/driver.h>

namespace gtos {
    namespace drivers {
        class MouseEventHandler {

            public:
                MouseEventHandler();

                virtual void OnActivate();
                virtual void OnMouseDown(uint8_t button);
                virtual void OnMouseUp(uint8_t button);
                virtual void OnMouseMove(int8_t x,int8_t y);
        };

        class MouseDriver : public hardwarecommunication::InterruptHandler , public Driver {
            hardwarecommunication::Port8Bit dataport;
            hardwarecommunication::Port8Bit commandport;

            uint8_t buffer[3];
            uint8_t offset;
            uint8_t buttons;

            MouseEventHandler* handler;

        public:
            MouseDriver(hardwarecommunication::InterruptsManager* manager, MouseEventHandler* handler);
            ~MouseDriver();
            virtual uint32_t HandlerInterrupt(uint32_t esp);
            virtual void Activate();
        };

    }
}


#endif