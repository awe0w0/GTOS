#include <drivers/nvme.h>

using namespace gtos::drivers;
using namespace gtos::hardwarecommunication;
using namespace gtos::memory;

#ifdef GTOS_NVME_TEST
namespace gtos { namespace drivers {
    void NvmeTestWriteRegister(NvmeDisk&, uint32_t, uint32_t);
} }
#endif

namespace {
    const uint32_t BaseDoorbell = 0x1000;
    const uint32_t PciInterruptDisable = 1u << 10;
    const uint32_t PciMemorySpace = 1u << 1;
    const uint32_t PciBusMaster = 1u << 2;
    const uint32_t Capabilities = 0x00;
    const uint32_t InterruptMaskSet = 0x0C;
    const uint32_t Configuration = 0x14;
    const uint32_t Status = 0x1C;
    const uint32_t AdminAttributes = 0x24;
    const uint32_t AdminSubmission = 0x28;
    const uint32_t AdminCompletion = 0x30;
    const uint32_t Enabled = 1;
    const uint32_t Ready = 1;
    const uint32_t Fatal = 2;
    const uint32_t PollLimit = 100000000;

    uint32_t Read32(const uint8_t* p) {
        return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
               ((uint32_t)p[3] << 24);
    }
}

NvmeDisk::NvmeDisk(PeripheralComponentInterconnectController& pci,
                   PhysicalMemoryManager& frames, uint32_t (*ticks)())
    : pci(pci), frames(frames), ticks(ticks), controller{}, registers(0), dma(0),
      dmaPages(0), readDepth(0),
      stride(0), readyTicks(0), maximumBytes(0), nsid(0), sectorBytes(0), entries(0),
      sectors(0), nextCommand(0), ready(false), error(NvmeOk), completionStatus(0),
      admin{}, io{} {}

bool NvmeDisk::Find(PeripheralComponentInterconnectController& pci, NvmeController& found,
                    bool allowHighBar) {
    found.physicalAddress = 0;
    found.address = found.bytes = 0;
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint16_t device = 0; device < 32; ++device) {
            const uint16_t vendor = pci.Read(bus, device, 0, 0);
            if (vendor == 0xFFFF || vendor == 0)
                continue;
            const uint16_t functions = pci.DeviceHasFunctions(bus, device) ? 8 : 1;
            for (uint16_t function = 0; function < functions; ++function) {
                // Mass storage / NVM / NVM Express controller.
                if (pci.Read(bus, device, function, 8) >> 8 != 0x010802)
                    continue;
                const auto d = pci.GetDeviceDescriptor(bus, device, function);
                uint64_t address;
                uint32_t bytes;
                if (pci.ProbeMemoryBar(d, 0, address, bytes) && bytes >= 8192 &&
                    bytes <= 16 * 1024 * 1024 && (allowHighBar || address < 0x100000000ULL)) {
                    found.pci = d;
                    found.physicalAddress = address;
                    found.address = address < 0x100000000ULL ? (uint32_t)address : 0;
                    found.bytes = bytes;
                    return true;
                }
            }
        }
    }
    return false;
}

void NvmeDisk::SetBusMaster(bool enabled) {
    const auto& d = controller.pci;
    uint16_t command = pci.Read(d.bus, d.device, d.function, 4);
    command |= PciMemorySpace | PciInterruptDisable;
    if (enabled)
        command |= PciBusMaster;
    else
        command &= ~PciBusMaster;
    pci.Write(d.bus, d.device, d.function, 4, command);
}

bool NvmeDisk::DisableMessageInterrupts() {
    const auto& d = controller.pci;
    if (!(pci.Read(d.bus, d.device, d.function, 4) & (1u << 20)))
        return true;
    uint32_t offset = pci.Read(d.bus, d.device, d.function, 0x34) & 0xFFu;
    for (uint32_t count = 0; count < 48 && offset; ++count) {
        if (offset < 0x40 || (offset & 3) || offset > 0xFC)
            return false;
        const uint32_t capability = pci.Read(d.bus, d.device, d.function, offset);
        const uint32_t id = capability & 0xFFu;
        if (id == 0x05)
            pci.Write(d.bus, d.device, d.function, offset, capability & ~(1u << 16));
        else if (id == 0x11)
            pci.Write(d.bus, d.device, d.function, offset, capability & ~(1u << 31));
        offset = (capability >> 8) & 0xFFu;
    }
    return !offset;
}

uint32_t NvmeDisk::Register(uint32_t offset) const {
    return registers[offset / 4];
}

void NvmeDisk::WriteRegister(uint32_t offset, uint32_t value) {
    registers[offset / 4] = value;
#ifdef GTOS_NVME_TEST
    gtos::drivers::NvmeTestWriteRegister(*this, offset, value);
#endif
}

bool NvmeDisk::WaitReady(bool expected) const {
    const uint32_t start = ticks();
    for (uint32_t attempts = 0; attempts < PollLimit; ++attempts) {
        const uint32_t value = Register(Status);
        if (value == 0xFFFFFFFFu || (expected && (value & Fatal)))
            return false;
        if (bool(value & Ready) == expected)
            return true;
        if (ticks() - start >= readyTicks)
            return false;
        asm volatile("pause");
    }
    return false;
}

bool NvmeDisk::Fail(NvmeError reason) {
    error = reason;
    Stop();
    return false;
}

uint32_t NvmeDisk::DataAddress(uint32_t slot) const {
    return dma + (QueuePages + slot * DataSlotPages + 1) * PageBytes;
}

bool NvmeDisk::Start(const NvmeController& selected, const KernelPaging& paging) {
    if (registers || dma || !ticks || !selected.address ||
        (selected.address & (PageBytes - 1)) ||
        (selected.bytes & (PageBytes - 1)) || selected.bytes < 8192 ||
        (uint64_t)selected.address + selected.bytes > 0x100000000ULL) {
        error = NvmeBadMapping;
        return false;
    }

    for (uint32_t offset = 0; offset < selected.bytes; offset += PageBytes) {
        const uint32_t address = selected.address + offset;
        PagingMapping mapping;
        if (!paging.query(address, mapping) || mapping.physicalAddress != selected.physicalAddress + offset ||
            !mapping.writable || mapping.userAccessible || !mapping.cacheDisabled) {
            error = NvmeBadMapping;
            return false;
        }
    }

    controller = selected;
    registers = (volatile uint32_t*)(__UINTPTR_TYPE__)selected.address;
    SetBusMaster(false);
    const uint32_t cap = Register(Capabilities);
    const uint32_t capHigh = Register(Capabilities + 4);
    stride = 4u << (capHigh & 15);
    const uint32_t deviceEntries = (cap & 0xFFFFu) + 1;
    entries = deviceEntries < MaximumQueueEntries ? deviceEntries : MaximumQueueEntries;
    const uint32_t timeoutUnits = cap >> 24;
    // CAP.TO is in 500 ms units. The kernel's PIT delivers 100 ticks per second.
    readyTicks = 50 * (timeoutUnits ? timeoutUnits : 1);
    if (cap == 0xFFFFFFFFu || !(capHigh & (1u << 5)) || (capHigh & (15u << 16)) ||
        BaseDoorbell + 3 * stride + 4 > selected.bytes || entries < 2 ||
        !DisableMessageInterrupts())
        return Fail(NvmeUnsupportedController);

    WriteRegister(Configuration, 0);
    if (!WaitReady(false))
        return Fail(NvmeControllerTimeout);
    // Keep one submission entry empty. A controller with fewer queue entries
    // or a machine with less contiguous low memory can still run at depth one.
    readDepth = entries - 1 < MaximumReadDepth ? entries - 1 : MaximumReadDepth;
    while (readDepth) {
        dmaPages = QueuePages + readDepth * DataSlotPages;
        if (frames.allocateContiguous(dmaPages, dma))
            break;
        --readDepth;
    }
    if (!readDepth)
        return Fail(NvmeNoDmaMemory);

    uint32_t* queuePages = (uint32_t*)(__UINTPTR_TYPE__)dma;
    for (uint32_t i = 0; i < QueuePages * PageBytes / sizeof(uint32_t); ++i)
        queuePages[i] = 0;
    // A slot always DMA-transfers to the same run of pages. The controller
    // consumes only the list entries covered by the command's block count.
    for (uint32_t slot = 0; slot < readDepth; ++slot) {
        const uint32_t address = DataAddress(slot);
        uint64_t* prps = (uint64_t*)(__UINTPTR_TYPE__)(address - PageBytes);
        for (uint32_t i = 0; i + 1 < DataPages; ++i)
            prps[i] = address + (i + 1) * PageBytes;
    }
    nextCommand = 0;
    admin = {dma, dma + PageBytes, 0, 0, 0, 1};
    io = {dma + 2 * PageBytes, dma + 3 * PageBytes, 1, 0, 0, 1};
    WriteRegister(InterruptMaskSet, 0xFFFFFFFFu);
    WriteRegister(AdminAttributes, (entries - 1) | ((entries - 1) << 16));
    WriteRegister(AdminSubmission, admin.submission);
    WriteRegister(AdminSubmission + 4, 0);
    WriteRegister(AdminCompletion, admin.completion);
    WriteRegister(AdminCompletion + 4, 0);
    __sync_synchronize();
    SetBusMaster(true);
    // NVM command set, 4 KiB memory page, 64-byte SQ and 16-byte CQ entries.
    WriteRegister(Configuration, Enabled | (6u << 16) | (4u << 20));
    if (!WaitReady(true))
        return Fail(NvmeControllerTimeout);
    if (!Identify() || !StartQueues())
        return Fail(error);
    ready = true;
    error = NvmeOk;
    return true;
}

uint16_t NvmeDisk::Enqueue(Queue& queue, Command& command) {
    const uint16_t cid = ++nextCommand;
    command.words[0] |= (uint32_t)cid << 16;
    volatile uint32_t* submission = (volatile uint32_t*)(__UINTPTR_TYPE__)queue.submission + 16 * queue.tail;
    for (uint32_t i = 0; i < 16; ++i)
        submission[i] = command.words[i];
    if (++queue.tail == entries)
        queue.tail = 0;
    return cid;
}

void NvmeDisk::RingSubmission(const Queue& queue) {
    __sync_synchronize();
    WriteRegister(BaseDoorbell + 2 * queue.id * stride, queue.tail);
}

bool NvmeDisk::Receive(Queue& queue, uint16_t& cid) {
    const uint32_t start = ticks();
    volatile uint32_t* completion = (volatile uint32_t*)(__UINTPTR_TYPE__)queue.completion + 4 * queue.head;
    for (uint32_t attempts = 0; attempts < PollLimit; ++attempts) {
        const uint32_t result = completion[3];
        if (((result >> 16) & 1) == queue.phase) {
            __sync_synchronize();
            const uint32_t queueInfo = completion[2];
            if ((queueInfo >> 16) != queue.id ||
                (uint16_t)queueInfo >= entries) {
                error = NvmeBadCompletion;
                return false;
            }
            cid = (uint16_t)result;
            completionStatus = (uint16_t)(result >> 16) & ~1u;
            if (++queue.head == entries) {
                queue.head = 0;
                queue.phase ^= 1;
            }

            error = completionStatus ? NvmeCommandFailed : NvmeOk;
            return !completionStatus;
        }
        if (Register(Status) & Fatal || ticks() - start >= 500)
            break;
        asm volatile("pause");
    }
    error = NvmeCommandTimeout;
    return false;
}

void NvmeDisk::RingCompletion(const Queue& queue) {
    WriteRegister(BaseDoorbell + (2 * queue.id + 1) * stride, queue.head);
}

bool NvmeDisk::Submit(Queue& queue, Command& command) {
    const uint16_t expected = Enqueue(queue, command);
    RingSubmission(queue);
    uint16_t observed;
    if (!Receive(queue, observed))
        return false;
    RingCompletion(queue);
    if (observed != expected) {
        error = NvmeBadCompletion;
        return false;
    }
    return true;
}

bool NvmeDisk::Identify() {
    Command identify = {};
    identify.words[0] = 0x06;
    identify.words[6] = DataAddress();
    identify.words[10] = 1;
    if (!Submit(admin, identify))
        return false;

    const uint8_t* data = (const uint8_t*)(__UINTPTR_TYPE__)DataAddress();
    const uint32_t mdts = data[77];
    maximumBytes = mdts && mdts < 7 ? PageBytes << mdts : DataPages * PageBytes;

    identify = {};
    identify.words[0] = 0x06;
    identify.words[6] = DataAddress();
    identify.words[10] = 2;
    if (!Submit(admin, identify))
        return false;
    nsid = Read32(data);
    if (!nsid || nsid == 0xFFFFFFFFu) {
        error = NvmeUnsupportedNamespace;
        return false;
    }

    identify = {};
    identify.words[0] = 0x06;
    identify.words[1] = nsid;
    identify.words[6] = DataAddress();
    if (!Submit(admin, identify))
        return false;
    const uint32_t format = data[26] & 15;
    const uint8_t* lbaf = data + 128 + format * 4;
    sectors = ((uint64_t)Read32(data + 4) << 32) | Read32(data);
    if (!sectors || format > data[25] || (data[26] & 0xE0u) || (data[29] & 7) ||
        lbaf[0] || lbaf[1] || (lbaf[2] != 9 && lbaf[2] != 12)) {
        error = NvmeUnsupportedNamespace;
        return false;
    }
    sectorBytes = 1u << lbaf[2];
    return true;
}

bool NvmeDisk::StartQueues() {
    Command command = {};
    command.words[0] = 0x09;
    command.words[10] = 7; // One I/O queue pair (zero-based).
    if (!Submit(admin, command))
        return false;

    command = {};
    command.words[0] = 0x05;
    command.words[6] = io.completion;
    command.words[10] = ((entries - 1) << 16) | io.id;
    command.words[11] = 1; // Physically contiguous; no interrupts.
    if (!Submit(admin, command))
        return false;

    command = {};
    command.words[0] = 0x01;
    command.words[6] = io.submission;
    command.words[10] = ((entries - 1) << 16) | io.id;
    command.words[11] = ((uint32_t)io.id << 16) | 1;
    return Submit(admin, command);
}

bool NvmeDisk::ReadSectors(uint64_t first, uint32_t count, uint8_t* destination) {
    return Transfer(first, count, destination, nullptr);
}

bool NvmeDisk::WriteSectors(uint64_t first, uint32_t count, const uint8_t* source) {
    return Transfer(first, count, nullptr, source);
}

bool NvmeDisk::Transfer(uint64_t first, uint32_t count, uint8_t* destination,
                        const uint8_t* source) {
    const uint64_t bytes = (uint64_t)sectorBytes * count;
    const uint64_t address = source ? (__UINTPTR_TYPE__)source : (__UINTPTR_TYPE__)destination;
    if (!ready || !count || (!destination && !source) || first >= sectors || count > sectors - first ||
        address >= 0x100000000ULL || bytes > 0x100000000ULL - address) {
        error = NvmeBadRequest;
        return false;
    }

    const uint32_t maxSectors = maximumBytes / sectorBytes;
    struct Pending {
        uint8_t* destination;
        uint32_t bytes;
        uint16_t cid;
        bool active;
    };
    while (count) {
        Pending pending[MaximumReadDepth];
        uint32_t posted = 0;
        while (count && posted < readDepth) {
            const uint32_t blockCount = count <= maxSectors ? count : maxSectors;
            const uint32_t transfer = blockCount * sectorBytes;
            const uint32_t pages = (transfer + PageBytes - 1) / PageBytes;
            const uint32_t address = DataAddress(posted);
            Command command = {};
            command.words[0] = source ? 0x01 : 0x02;
            command.words[1] = nsid;
            command.words[6] = address;
            if (pages == 2)
                command.words[8] = address + PageBytes;
            else if (pages > 2)
                command.words[8] = address - PageBytes;
            command.words[10] = (uint32_t)first;
            command.words[11] = (uint32_t)(first >> 32);
            command.words[12] = blockCount - 1;
            if (source) {
                // Copy before posting. An NVMe write must not DMA from a
                // caller buffer that a later task can change or release.
                uint8_t* to = (uint8_t*)(__UINTPTR_TYPE__)address;
                const uint8_t* from = source;
                uint32_t words = transfer / sizeof(uint32_t);
                asm volatile("cld; rep movsl" : "+D"(to), "+S"(from), "+c"(words) : : "memory", "cc");
            }
            pending[posted++] = {destination, transfer, Enqueue(io, command), true};
            first += blockCount;
            count -= blockCount;
            if (source) source += transfer;
            else destination += transfer;
        }

        RingSubmission(io);
        for (uint32_t completed = 0; completed < posted; ++completed) {
            uint16_t cid;
            if (!Receive(io, cid))
                return Fail(error);
            uint32_t slot = 0;
            while (slot < posted && (!pending[slot].active || pending[slot].cid != cid))
                ++slot;
            if (slot == posted)
                return Fail(NvmeBadCompletion);

            // Supported LBAs are multiples of a word. Copy only completed data
            // into caller storage, including when completions arrive out of order.
            if (pending[slot].destination) {
                uint8_t* to = pending[slot].destination;
                const uint8_t* from = (const uint8_t*)(__UINTPTR_TYPE__)DataAddress(slot);
                uint32_t words = pending[slot].bytes / sizeof(uint32_t);
                asm volatile("cld; rep movsl" : "+D"(to), "+S"(from), "+c"(words) : : "memory", "cc");
            }
            pending[slot].active = false;
        }
        // The batch uses fewer entries than the CQ can hold.
        RingCompletion(io);
    }
    return true;
}

bool NvmeDisk::Flush() {
    if (!ready) {
        error = NvmeBadRequest;
        return false;
    }
    Command command = {};
    command.words[1] = nsid;
    return Submit(io, command) || Fail(error);
}

bool NvmeDisk::Stop() {
    ready = false;
    sectorBytes = 0;
    sectors = 0;
    if (!registers)
        return !dma;

    WriteRegister(Configuration, 0);
    const bool stopped = WaitReady(false);
    SetBusMaster(false);
    registers = 0;
    if (stopped && dma && frames.freeContiguous(dma, dmaPages)) {
        dma = dmaPages = 0;
        readDepth = 0;
    }
    return stopped && !dma;
}
