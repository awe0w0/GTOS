#ifndef __GTOS__SYSCALLS_H
#define __GTOS__SYSCALLS_H

#include <common/types.h>
#include <hardwarecommunication/interrupts.h>
#include <multitasking.h>

namespace gtos {
    class SyscallHandler : public hardwarecommunication::InterruptHandler {
    public:
        SyscallHandler(hardwarecommunication::InterruptsManager* interruptManager, uint8_t InterruptNumber);
        ~SyscallHandler();

        virtual uint32_t HandlerInterrupt(uint32_t esp);
    };
}

#endif