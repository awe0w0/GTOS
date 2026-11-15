#ifndef __GTOS__DRIVERS__AMD_AM79C973_H
#define __GTOS__DRIVERS__AMD_AM79C973_H

#include <common/types.h>
#include <drivers/driver.h>
#include <hardwarecommunication/interrupts.h>
#include <hardwarecommunication/pci.h>
#include <hardwarecommunication/port.h>


namespace gtos {
    namespace drivers {
        class amd_am79c973;

        class RawDataHandler {
        protected:
            amd_am79c973* backend;
        public:
            RawDataHandler(amd_am79c973* backend);
            ~RawDataHandler();

            virtual bool OnRawDataReceived(uint8_t* buffer, uint32_t size);
            void Send(uint8_t* buffer, uint32_t size);
        };

        class amd_am79c973 : public Driver , public hardwarecommunication::InterruptHandler {
            static const unsigned RingOrder = 3;
            static const unsigned RingSize = 1U << RingOrder;
            static const unsigned BufferBytes = 2048;

            struct InitializationBlock {
                uint16_t mode;
                uint8_t recvRingLength;
                uint8_t sendRingLength;
                uint8_t physicalAddress[6];
                uint16_t reserved;
                uint32_t multicastFilter[2];
                uint32_t recvBufferDescrAddress;
                uint32_t sendBufferDescrAddress;
            };
            static_assert(sizeof(InitializationBlock) == 28, "PCnet style 2 init block");
            
            //缓冲区指针
            struct BufferDescriptor {
                uint32_t address;
                volatile uint32_t flags;
                volatile uint32_t flags2;
                uint32_t avail;
            };
            static_assert(sizeof(BufferDescriptor) == 16, "PCnet style 2 descriptor");

            hardwarecommunication::Port16Bit MACAddress0Port;
            hardwarecommunication::Port16Bit MACAddress2Port;
            hardwarecommunication::Port16Bit MACAddress4Port;
            hardwarecommunication::Port16Bit registerDataPort;
            hardwarecommunication::Port16Bit registerAddressPort;
            hardwarecommunication::Port16Bit resetPort;
            hardwarecommunication::Port16Bit busControlRegisterDataPort;
            
            InitializationBlock initBlock;

            BufferDescriptor* sendBufferDescr;
            uint8_t sendBufferDescrMemory[RingSize * sizeof(BufferDescriptor) + 15];
            uint8_t sendBuffers[RingSize][BufferBytes + 15];
            uint8_t currentSendBuffer;

            BufferDescriptor* recvBufferDescr;
            uint8_t recvBufferDescrMemory[RingSize * sizeof(BufferDescriptor) + 15];
            uint8_t recvBuffers[RingSize][BufferBytes + 15];
            uint8_t currentRecvBuffer;

            uint32_t ipAddress;
            RawDataHandler* handler;
            bool active;

            uint16_t ReadRegister(uint16_t index);
            void WriteRegister(uint16_t index, uint16_t value);

            public:
                amd_am79c973(hardwarecommunication::PeripheralComponentInterconnectDeviceDescriptor* dev, hardwarecommunication::InterruptsManager* interrupts);
                ~amd_am79c973();
                
                void Activate();
                int Reset();
                uint32_t HandlerInterrupt(uint32_t esp);

                void Send(uint8_t* buffer, int count);
                void Receive();

                void SetHandler(RawDataHandler* handler);
                uint64_t GetMACAddress();

                void SetIPAddress(uint32_t);
                uint32_t GetIPAddress();
        };
    }
}

#endif
