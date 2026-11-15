#include <drivers/amd_am79c973.h>
#include <memory/criticalsection.h>

using namespace gtos;
using namespace gtos::drivers;
using namespace gtos::hardwarecommunication;

namespace {
    const uint32_t OwnedByDevice = 0x80000000U;
    const uint32_t DescriptorError = 0x40000000U;
    const uint32_t WholeFrame = 0x03000000U;
    const unsigned EthernetHeaderBytes = 14;
    const unsigned MinimumFrameBytes = 60;
    const unsigned MaximumFrameBytes = 1514;
    const unsigned FcsBytes = 4;
    const uint16_t Initialize = 0x0001U;
    const uint16_t Start = 0x0002U;
    const uint16_t Stop = 0x0004U;
    const uint16_t Transmit = 0x0008U;
    const uint16_t InterruptEnable = 0x0040U;
    const uint16_t InterruptPending = 0x0080U;
    const uint16_t Initialized = 0x0100U;
    const uint16_t ReceiveInterrupt = 0x0400U;
    const uint16_t InterruptReasons = 0x7f00U;
    const uint16_t DeviceError = 0x8000U;

    uint32_t DmaAddress(uint8_t* memory) {
        return (reinterpret_cast<uint32_t>(memory) + 15U) & ~15U;
    }

    uint32_t EncodedLength(unsigned bytes) {
        return 0xf000U | ((0U - bytes) & 0xfffU);
    }
}

RawDataHandler::RawDataHandler(amd_am79c973* backend) {
    this->backend = backend;
    backend->SetHandler(this);
}

RawDataHandler::~RawDataHandler() {
    backend->SetHandler(0);
}

bool RawDataHandler::OnRawDataReceived(uint8_t*, uint32_t) {
    return false;
}

void RawDataHandler::Send(uint8_t* buffer, uint32_t size) {
    if (size <= MaximumFrameBytes) backend->Send(buffer, size);
}

amd_am79c973::amd_am79c973(PeripheralComponentInterconnectDeviceDescriptor* dev, InterruptsManager* interrupts)
: Driver(),
InterruptHandler(interrupts, dev->interrupt + interrupts->HardwareInterruptOffset()),
MACAddress0Port(dev->portBase),
MACAddress2Port(dev->portBase + 0x02),
MACAddress4Port(dev->portBase + 0x04),
registerDataPort(dev->portBase + 0x10),
registerAddressPort(dev->portBase + 0x12),
resetPort(dev->portBase + 0x14),
busControlRegisterDataPort(dev->portBase + 0x16),
initBlock(),
sendBufferDescr(reinterpret_cast<BufferDescriptor*>(DmaAddress(sendBufferDescrMemory))),
currentSendBuffer(0),
recvBufferDescr(reinterpret_cast<BufferDescriptor*>(DmaAddress(recvBufferDescrMemory))),
currentRecvBuffer(0),
ipAddress(0),
handler(0),
active(false)
{
    const uint16_t words[] = {MACAddress0Port.Read(), MACAddress2Port.Read(), MACAddress4Port.Read()};
    for (unsigned i = 0; i < 3; ++i) {
        initBlock.physicalAddress[i * 2] = static_cast<uint8_t>(words[i]);
        initBlock.physicalAddress[i * 2 + 1] = static_cast<uint8_t>(words[i] >> 8);
    }
    initBlock.sendRingLength = RingOrder << 4;
    initBlock.recvRingLength = RingOrder << 4;
    initBlock.sendBufferDescrAddress = reinterpret_cast<uint32_t>(sendBufferDescr);
    initBlock.recvBufferDescrAddress = reinterpret_cast<uint32_t>(recvBufferDescr);
}

amd_am79c973::~amd_am79c973() {
    memory::InterruptGuard guard;
    if (active) WriteRegister(0, Stop);
}

uint16_t amd_am79c973::ReadRegister(uint16_t index) {
    registerAddressPort.Write(index);
    return registerDataPort.Read();
}

void amd_am79c973::WriteRegister(uint16_t index, uint16_t value) {
    registerAddressPort.Write(index);
    registerDataPort.Write(value);
}

void amd_am79c973::Activate() {
    memory::InterruptGuard guard;
    Reset();
    currentSendBuffer = currentRecvBuffer = 0;

    // Style 2 uses 32-bit bus addresses and sixteen-byte descriptors.
    registerAddressPort.Write(20);
    busControlRegisterDataPort.Write(0x0102);
    // Keep the FCS in receive buffers. MCNT then always includes four FCS bytes.
    WriteRegister(4, ReadRegister(4) & ~0x0800U);

    for (unsigned i = 0; i < RingSize; ++i) {
        sendBufferDescr[i].address = DmaAddress(sendBuffers[i]);
        sendBufferDescr[i].flags = EncodedLength(BufferBytes);
        sendBufferDescr[i].flags2 = 0;
        sendBufferDescr[i].avail = 0;
        recvBufferDescr[i].address = DmaAddress(recvBuffers[i]);
        recvBufferDescr[i].flags2 = 0;
        recvBufferDescr[i].avail = 0;
        recvBufferDescr[i].flags = OwnedByDevice | EncodedLength(BufferBytes);
    }

    const uint32_t address = reinterpret_cast<uint32_t>(&initBlock);
    WriteRegister(1, static_cast<uint16_t>(address));
    WriteRegister(2, static_cast<uint16_t>(address >> 16));
    asm volatile("" : : : "memory");
    WriteRegister(0, Initialize);
    for (unsigned attempt = 0; attempt < 10000; ++attempt) {
        const uint16_t status = ReadRegister(0);
        if (status & DeviceError) break;
        if (status & Initialized) {
            interruptManager->Load(this, interruptNumber);
            active = true;
            WriteRegister(0, Initialized | InterruptEnable | Start);
            return;
        }
    }
    WriteRegister(0, Stop);
}

int amd_am79c973::Reset() {
    memory::InterruptGuard guard;
    active = false;
    resetPort.Read();
    return 10;
}

uint32_t amd_am79c973::HandlerInterrupt(uint32_t esp) {
    if (!active) return esp;
    const uint16_t status = ReadRegister(0);
    if (!(status & InterruptPending)) return esp;

    // Acknowledge only observed events and preserve interrupt delivery.
    WriteRegister(0, (status & InterruptReasons) | InterruptEnable);
    if (status & ReceiveInterrupt) Receive();
    return esp;
}

void amd_am79c973::Send(uint8_t* buffer, int size) {
    if (!buffer || size < static_cast<int>(EthernetHeaderBytes)
        || size > static_cast<int>(MaximumFrameBytes)) return;
    memory::InterruptGuard guard;
    if (!active) return;

    BufferDescriptor& descriptor = sendBufferDescr[currentSendBuffer];
    if (descriptor.flags & OwnedByDevice) return;
    asm volatile("" : : : "memory");

    uint8_t* output = reinterpret_cast<uint8_t*>(descriptor.address);
    const unsigned bytes = static_cast<unsigned>(size);
    const unsigned frameBytes = bytes < MinimumFrameBytes ? MinimumFrameBytes : bytes;
    for (unsigned i = 0; i < bytes; ++i) output[i] = buffer[i];
    for (unsigned i = bytes; i < frameBytes; ++i) output[i] = 0;

    descriptor.avail = 0;
    descriptor.flags2 = 0;
    asm volatile("" : : : "memory");
    descriptor.flags = OwnedByDevice | WholeFrame | EncodedLength(frameBytes);
    currentSendBuffer = (currentSendBuffer + 1) % RingSize;
    WriteRegister(0, InterruptEnable | Transmit);
}

void amd_am79c973::Receive() {
    memory::InterruptGuard guard;
    if (!active) return;
    for (unsigned i = 0; i < RingSize; ++i) {
        BufferDescriptor& descriptor = recvBufferDescr[currentRecvBuffer];
        const uint32_t status = descriptor.flags;
        if (status & OwnedByDevice) break;
        asm volatile("" : : : "memory");
        const unsigned bytesWithFcs = descriptor.flags2 & 0xfffU;
        if (handler && !(status & DescriptorError) && (status & WholeFrame) == WholeFrame
            && bytesWithFcs >= EthernetHeaderBytes + FcsBytes
            && bytesWithFcs <= MaximumFrameBytes + FcsBytes) {
            uint8_t* buffer = reinterpret_cast<uint8_t*>(descriptor.address);
            const unsigned bytes = bytesWithFcs - FcsBytes;
            if (handler->OnRawDataReceived(buffer, bytes)) Send(buffer, bytes);
        }
        descriptor.flags2 = 0;
        descriptor.avail = 0;
        asm volatile("" : : : "memory");
        descriptor.flags = OwnedByDevice | EncodedLength(BufferBytes);
        currentRecvBuffer = (currentRecvBuffer + 1) % RingSize;
    }
}

void amd_am79c973::SetHandler(RawDataHandler* handler) {
    memory::InterruptGuard guard;
    this->handler = handler;
}

uint64_t amd_am79c973::GetMACAddress() {
    uint64_t result = 0;
    for (int i = 5; i >= 0; --i) result = (result << 8) | initBlock.physicalAddress[i];
    return result;
}

void amd_am79c973::SetIPAddress(uint32_t ip) {
    ipAddress = ip;
}

uint32_t amd_am79c973::GetIPAddress() {
    return ipAddress;
}
