#ifndef __GTOS__HARDWARECOMMUNICATION__INTERRUPTS_H
#define __GTOS__HARDWARECOMMUNICATION__INTERRUPTS_H

#include <common/types.h>
#include <hardwarecommunication/port.h>
#include <gdt.h>
#include <multitasking.h>

namespace gtos {
    namespace hardwarecommunication {
        class InterruptsManager;

        class InterruptHandler {
            protected:
                uint8_t interruptNumber;
                InterruptsManager* interruptManager;

                InterruptHandler(InterruptsManager* InterruptManager, uint8_t InterruptNumber);
                ~InterruptHandler();
            public:
                virtual uint32_t HandlerInterrupt(uint32_t esp);
        };

        class InterruptsManager{
            friend class InterruptHandler;
            protected:
                static InterruptsManager* ActivateInterruptsManager;
                InterruptHandler* handlers[256];
                TaskManager* taskManager;
                
                

                struct GateDescriptor{
                    uint16_t handlerAddressLowBits;
                    uint16_t gdt_codeSegmentSelector;
                    uint8_t reserved;
                    uint8_t access;
                    uint16_t handlerAddressHighBits;
                } __attribute__((packed));
                
                static GateDescriptor interruptDescriptorTable[256];
                
                struct InterruptDescriptorTablePointer {
                    uint16_t size;
                    uint32_t base;
                } __attribute__((packed));

                uint16_t hardwareInterruptOffset;
                

                static void SetInterruptDescriptorTableEntry(
                    uint8_t interruptNumber,
                    uint16_t codeSegmentSelectorOffset,
                    void (*handler)(),
                    uint8_t DescriptorPrivilegeLevel,
                    uint8_t DescriptorType
                );
                static void InterruptIgnore();

                static void HandleInterruptRequest0x00();
                static void HandleInterruptRequest0x01();
                static void HandleInterruptRequest0x02();
                static void HandleInterruptRequest0x03();
                static void HandleInterruptRequest0x04();
                static void HandleInterruptRequest0x05();
                static void HandleInterruptRequest0x06();
                static void HandleInterruptRequest0x07();
                static void HandleInterruptRequest0x08();
                static void HandleInterruptRequest0x09();
                static void HandleInterruptRequest0x0A();
                static void HandleInterruptRequest0x0B();
                static void HandleInterruptRequest0x0C();
                static void HandleInterruptRequest0x0D();
                static void HandleInterruptRequest0x0E();
                static void HandleInterruptRequest0x0F();
                static void HandleInterruptRequest0x31();

                static void HandleException0x00();
                static void HandleException0x01();
                static void HandleException0x02();
                static void HandleException0x03();
                static void HandleException0x04();
                static void HandleException0x05();
                static void HandleException0x06();
                static void HandleException0x07();
                static void HandleException0x08();
                static void HandleException0x09();
                static void HandleException0x0A();
                static void HandleException0x0B();
                static void HandleException0x0C();
                static void HandleException0x0D();
                static void HandleException0x0E();
                static void HandleException0x0F();
                static void HandleException0x10();
                static void HandleException0x11();
                static void HandleException0x12();
                static void HandleException0x13();

                static uint32_t handleInterrupt(uint8_t interruptNamber, uint32_t esp);
                uint32_t DoHandleInterrupt(uint8_t interruptNamber, uint32_t esp);

                Port8BitSlow picMasterCommand;
                Port8BitSlow picMasterData;
                Port8BitSlow picSlaveCommand;
                Port8BitSlow picSlaveData;

            public:
                
                InterruptsManager(uint16_t hardwareInterruptOffset, GlobalDescriptorTable* gdt, gtos::TaskManager* taskmanager);
                ~InterruptsManager();
                uint16_t HardwareInterruptOffset();
                void Activate();
                void Deactivate();
                void Load(InterruptHandler* handler,uint8_t interruptNumber);
                void Load(TaskManager* taskManager);
                void Load(uint8_t InterruptNumber, InterruptsManager* InterruptManager);
                

        };

    }
}


#endif