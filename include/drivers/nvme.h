#ifndef GTOS_DRIVERS_NVME_H
#define GTOS_DRIVERS_NVME_H

#include <hardwarecommunication/pci.h>
#include <memory/paging.h>
#include <storage/readable_disk.h>

namespace gtos { namespace drivers {
    struct NvmeController {
        hardwarecommunication::PeripheralComponentInterconnectDeviceDescriptor pci;
        uint32_t address, bytes;
    };

    enum NvmeError {
        NvmeOk, NvmeBadMapping, NvmeUnsupportedController, NvmeNoDmaMemory,
        NvmeControllerTimeout, NvmeCommandTimeout, NvmeBadCompletion, NvmeCommandFailed,
        NvmeUnsupportedNamespace, NvmeBadRequest
    };

    // One polled NVM queue on the BSP. DMA never targets caller-owned storage.
    class NvmeDisk : public storage::ReadableDisk {
        static const uint32_t PageBytes = 4096;
        static const uint32_t DataPages = 128;
        static const uint32_t QueuePages = 4;
        static const uint32_t DataSlotPages = 1 + DataPages;
        static const uint32_t MaximumReadDepth = 4;
        static const uint32_t MaximumQueueEntries = 16;

        struct Command {
            uint32_t words[16];
        };
        struct Queue {
            uint32_t submission, completion;
            uint16_t id, tail, head, phase;
        };

        hardwarecommunication::PeripheralComponentInterconnectController& pci;
        memory::PhysicalMemoryManager& frames;
        uint32_t (*ticks)();
        NvmeController controller;
        volatile uint32_t* registers;
        uint32_t dma, dmaPages, readDepth, stride, readyTicks, maximumBytes, nsid, sectorBytes, entries;
        uint64_t sectors;
        uint16_t nextCommand;
        bool ready;
        NvmeError error;
        uint16_t completionStatus;
        Queue admin, io;

        void SetBusMaster(bool enabled);
        bool DisableMessageInterrupts();
        uint32_t Register(uint32_t offset) const;
        void WriteRegister(uint32_t offset, uint32_t value);
        bool WaitReady(bool expected) const;
        uint16_t Enqueue(Queue& queue, Command& command);
        void RingSubmission(const Queue& queue);
        bool Receive(Queue& queue, uint16_t& cid);
        void RingCompletion(const Queue& queue);
        bool Submit(Queue& queue, Command& command);
        bool StartQueues();
        bool Identify();
        bool Fail(NvmeError reason);
        uint32_t DataAddress(uint32_t slot = 0) const;
        NvmeDisk(const NvmeDisk&) = delete;
        NvmeDisk& operator=(const NvmeDisk&) = delete;

    public:
        NvmeDisk(hardwarecommunication::PeripheralComponentInterconnectController& pci,
                 memory::PhysicalMemoryManager& frames, uint32_t (*ticks)());
        static bool Find(hardwarecommunication::PeripheralComponentInterconnectController& pci,
                         NvmeController& controller);
        bool Start(const NvmeController& controller, const memory::KernelPaging& paging);
        // DMA frames are retained if the controller does not acknowledge reset.
        bool Stop();
        uint32_t LogicalSectorBytes() const override { return sectorBytes; }
        uint64_t LogicalSectorCount() const override { return sectors; }
        bool ReadSectors(uint64_t first, uint32_t count, uint8_t* destination) override;
        uint32_t MaxReadBytes() const { return maximumBytes; }
        uint32_t ReadDepth() const { return readDepth; }
        NvmeError Error() const { return error; }
        uint16_t CompletionStatus() const { return completionStatus; }
    };
} }

#endif
