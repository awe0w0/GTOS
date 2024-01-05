#ifndef __INTERRUPTS_H
#define __INTERRUPTS_H

#include "types.h"
#include "port.h"
#include "gdt.h"

class InterruptsManager;

class InterruptHandler {
    protected:
        uint8_t interruptNumber;
        InterruptsManager* interruptManager;

        InterruptHandler(uint8_t InterruptNumber, InterruptsManager* InterruptManager);
        ~InterruptHandler();
    public:
        virtual uint32_t HandlerInterrupt(uint32_t esp);
};

class InterruptsManager{
    friend InterruptHandler;
    protected:
        static InterruptsManager* ActivateInterruptsManager;
        InterruptHandler* handlers[256];
        struct GateDescriptor{
            uint16_t handlerAddressLowBits;
            uint16_t gdt_codeSegmentSelector;
            uint8_t reserved;
            uint8_t access;
            uint16_t handlerAddressHighBits;
        } __attribute__((packed));

        struct InterruptDescriptorTablePointer {
            uint16_t size;
            uint32_t base;
        } __attribute__((packed));

        static GateDescriptor interruptDescriptorTable[256];

        static void SetInterruptDescriptorTableEntry(
            uint8_t interruptNumber,
            uint16_t codeSegmentSelectorOffset,
            void (*handler)(),
            uint8_t DescriptorPrivilegeLevel,
            uint8_t DescriptorType
        );

        Port8BitSlow picMasterCommand;
        Port8BitSlow picMasterData;
        Port8BitSlow picSlaveCommand;
        Port8BitSlow picSlaveData;
    public:
        InterruptsManager(GlobalDescriptorTable* gdt);
        ~InterruptsManager();

        void Activate();
        void Deactivate();

        static uint32_t handleInterrupt(uint8_t interruptNamber, uint32_t esp);
        uint32_t DoHandleInterrupt(uint8_t interruptNamber, uint32_t esp);

        static void IgnoreInterruptRequest();
        static void HandleInterruptRequest0x00();
        static void HandleInterruptRequest0x01();
};

#endif