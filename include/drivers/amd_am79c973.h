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
            //初始化
            struct InitializationBlock {
                uint16_t mode;
                unsigned reserved1 : 4;
                unsigned numSendBuffers : 4;
                unsigned reserved2 : 4;
                unsigned numRecvBuffers : 4;
                uint64_t physicalAddress : 48;
                uint16_t reserved3;
                uint64_t logicalAddress;
                uint32_t recvBufferDescrAddress;
                uint32_t sendBufferDescrAddress;
            } __attribute__((packed));
            
            //缓冲区指针
            struct BufferDescriptor {
                uint32_t address;
                uint32_t flags;
                uint32_t flags2;
                uint32_t avail;
            } __attribute__((packed));

            hardwarecommunication::Port16Bit MACAddress0Port;
            hardwarecommunication::Port16Bit MACAddress2Port;
            hardwarecommunication::Port16Bit MACAddress4Port;
            hardwarecommunication::Port16Bit registerDataPort;
            hardwarecommunication::Port16Bit registerAddressPort;
            hardwarecommunication::Port16Bit resetPort;
            hardwarecommunication::Port16Bit busControlRegisterDataPort;
            
            InitializationBlock initBlock;

            BufferDescriptor* sendBufferDescr;
            uint8_t sendBufferDescrMemory[2048 + 15];
            uint8_t sendBuffers[2 * 1024 + 15][8];
            uint8_t currentSendBuffer;

            BufferDescriptor* recvBufferDescr;
            uint8_t recvBufferDescrMemory[2048 + 15];
            uint8_t recvBuffers[2 * 1024 + 15][8];
            uint8_t currentRecvBuffer;

            RawDataHandler* handler;

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