#include <drivers/amd_am79c973.h>
#include <gdt.h>
#include <memory/criticalsection.h>
#include <memorymanagement.h>
#include <net/icmp.h>

using namespace gtos;
using namespace gtos::drivers;
using namespace gtos::hardwarecommunication;
using namespace gtos::net;

void printf(char* message) {
    Port8Bit port(0xe9);
    while (*message) port.Write(*message++);
}

void printfHex(uint8_t value) {
    static const char digits[] = "0123456789abcdef";
    Port8Bit port(0xe9);
    port.Write(digits[value >> 4]);
    port.Write(digits[value & 15]);
}

void printfHex32(uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) printfHex(value >> shift);
}

namespace {
    const unsigned BurstSize = 8;
    const unsigned FrameSizes[BurstSize] = {42, 60, 257, 1514, 64, 680, 1300, 1492};
    alignas(16) uint8_t driverMemory[128 * 1024];
    const uint32_t LeftIP = 0x010200c0;
    const uint32_t RightIP = 0x020200c0;
    const uint8_t ExtendedEcho[] = {0x34, 0x12, 0x00, 0x01, 0xaa, 0x00, 0xdd, 0x77, 0x96};

    void Finish(bool pass) __attribute__((noreturn));
    void Finish(bool pass) {
        printf(pass ? (char*)"PCNET GUEST PASS\n" : (char*)"PCNET GUEST FAIL\n");
        Port32Bit exitPort(0xf4);
        exitPort.Write(pass ? 0x10 : 0x20);
        for (;;) asm volatile("cli; hlt");
    }

    void Require(bool condition, const char* message) {
        if (condition) return;
        printf((char*)message);
        printf((char*)"\n");
        Finish(false);
    }

    uint8_t Payload(unsigned frame, unsigned offset) {
        return static_cast<uint8_t>((frame * 41 + offset * 13) ^ (offset >> 5));
    }

    class Receiver : public RawDataHandler {
        uint64_t localMac;
        uint64_t peerMac;
    public:
        volatile unsigned received;
        volatile unsigned errors;

        Receiver(amd_am79c973& device, uint64_t peer)
        : RawDataHandler(&device), localMac(device.GetMACAddress()), peerMac(peer),
          received(0), errors(0) {}

        bool OnRawDataReceived(uint8_t* data, uint32_t bytes) {
            const unsigned index = received++;
            const unsigned length = FrameSizes[index % BurstSize];
            const unsigned expectedBytes = length < 60 ? 60 : length;
            if (bytes != expectedBytes) {
                ++errors;
                return false;
            }
            for (unsigned i = 0; i < 6; ++i) {
                if (data[i] != static_cast<uint8_t>(localMac >> (8 * i))
                    || data[i + 6] != static_cast<uint8_t>(peerMac >> (8 * i))) ++errors;
            }
            if (data[12] != 0x88 || data[13] != 0xb5) ++errors;
            for (unsigned i = 14; i < bytes; ++i) {
                if (data[i] != (i < length ? Payload(index, i) : 0)) ++errors;
            }
            return false;
        }
    };

    class NetworkInterrupt : public InterruptHandler {
        amd_am79c973& first;
        amd_am79c973* shared;
    public:
        NetworkInterrupt(InterruptsManager& manager, unsigned irq, amd_am79c973& device,
                         amd_am79c973* second)
        : InterruptHandler(&manager, irq), first(device), shared(second) {}

        uint32_t HandlerInterrupt(uint32_t esp) {
            esp = first.HandlerInterrupt(esp);
            return shared ? shared->HandlerInterrupt(esp) : esp;
        }
    };

    PeripheralComponentInterconnectDeviceDescriptor FindAdapter(
        PeripheralComponentInterconnectController& pci, unsigned slot) {
        auto device = pci.GetDeviceDescriptor(0, slot, 0);
        Require(device.vendor_id == 0x1022 && device.device_id == 0x2000, "PCnet PCI identity");
        const auto bar = pci.GetBaseAddressRegister(0, slot, 0, 0);
        Require(bar.type == InputOutput && bar.address, "PCnet I/O BAR");
        device.portBase = reinterpret_cast<uint32_t>(bar.address);
        device.interrupt &= 0xffU;
        Require(device.interrupt >= 3 && device.interrupt < 16, "PCnet PCI interrupt");
        return device;
    }

    void Unmask(unsigned irq) {
        Port8Bit mask(irq >= 8 ? 0xa1 : 0x21);
        mask.Write(mask.Read() & ~(1U << (irq % 8)));
    }

    void SendBurst(amd_am79c973& source, uint64_t destination, unsigned start) {
        uint8_t frame[1514];
        const uint64_t sourceMac = source.GetMACAddress();
        for (unsigned i = 0; i < 6; ++i) {
            frame[i] = static_cast<uint8_t>(destination >> (8 * i));
            frame[i + 6] = static_cast<uint8_t>(sourceMac >> (8 * i));
        }
        frame[12] = 0x88;
        frame[13] = 0xb5;
        // Give DMA all eight frames before the receiver is allowed to read them.
        memory::InterruptGuard guard;
        for (unsigned frameIndex = 0; frameIndex < BurstSize; ++frameIndex) {
            const unsigned size = FrameSizes[frameIndex];
            for (unsigned i = 14; i < size; ++i) frame[i] = Payload(start + frameIndex, i);
            source.Send(frame, size);
        }
    }

    void WaitFor(const Receiver& receiver, unsigned expected) {
        for (unsigned attempt = 0; attempt < 1000000 && receiver.received < expected; ++attempt)
            asm volatile("pause");
        Require(receiver.received == expected, "PCnet frame count");
        Require(receiver.errors == 0, "PCnet receive length, payload or padding");
    }

    class EchoReceiver : public InternetControlMessageProtocol {
    public:
        volatile unsigned replies;

        explicit EchoReceiver(InternetProtocolProvider& ip)
        : InternetControlMessageProtocol(&ip), replies(0) {}

        bool OnInternetProtocolReceived(uint32_t source, uint32_t destination,
                                        uint8_t* data, uint32_t size) {
            Require(source == RightIP && destination == LeftIP, "ICMP source and destination");
            Require(replies < 2 && size == (replies ? sizeof(ExtendedEcho) + 4 : 8),
                    "ICMP reply size and count");
            Require(data[0] == 0 && data[1] == 0 && !InternetProtocolProvider::Checksum(data, size),
                    "ICMP reply type and checksum");
            const uint8_t simple[] = {0x13, 0x37, 0, 0};
            const uint8_t* expected = replies ? ExtendedEcho : simple;
            for (unsigned i = 4; i < size; ++i)
                Require(data[i] == expected[i - 4], "ICMP echoed identifier, sequence and payload");
            ++replies;
            return InternetControlMessageProtocol::OnInternetProtocolReceived(source, destination, data, size);
        }
    };

    class EchoServer : public InternetControlMessageProtocol {
    public:
        volatile unsigned requests;
        volatile unsigned badChecksum;

        explicit EchoServer(InternetProtocolProvider& ip)
        : InternetControlMessageProtocol(&ip), requests(0), badChecksum(0) {}

        bool OnInternetProtocolReceived(uint32_t source, uint32_t destination,
                                        uint8_t* data, uint32_t size) {
            ++requests;
            if (InternetProtocolProvider::Checksum(data, size)) ++badChecksum;
            return InternetControlMessageProtocol::OnInternetProtocolReceived(source, destination, data, size);
        }
    };

    void VerifyIpv4(amd_am79c973& left, amd_am79c973& right) {
        left.SetIPAddress(LeftIP);
        right.SetIPAddress(RightIP);
        EtherFrameProvider leftLink(&left), rightLink(&right);
        AddressResolutionProtocol leftArp(&leftLink), rightArp(&rightLink);
        InternetProtocolProvider leftIp(&leftLink, &leftArp, 0, 0x00ffffff),
                                 rightIp(&rightLink, &rightArp, 0, 0x00ffffff);
        EchoReceiver client(leftIp);
        EchoServer server(rightIp);
        uint64_t mac = 0;
        Require(!leftArp.Resolve(RightIP, mac), "ARP starts with a cold neighbor");
        for (unsigned attempt = 0; attempt < 64 && !mac; ++attempt) {
            for (unsigned i = 0; i < 1024; ++i) asm volatile("pause");
            leftArp.Resolve(RightIP, mac);
        }
        Require(mac == right.GetMACAddress(), "ARP learned the peer over Ethernet");
        client.RequestEchoReply(RightIP);
        for (unsigned i = 0; i < 1000000 && client.replies < 1; ++i) asm volatile("pause");
        Require(client.replies == 1, "ICMP fixed-size probe reply");

        uint8_t packet[4 + sizeof(ExtendedEcho)] = {8, 0, 0, 0};
        for (unsigned i = 0; i < sizeof(ExtendedEcho); ++i) packet[i + 4] = ExtendedEcho[i];
        const uint16_t checksum = InternetProtocolProvider::Checksum(packet, sizeof(packet));
        packet[2] = static_cast<uint8_t>(checksum);
        packet[3] = static_cast<uint8_t>(checksum >> 8);
        packet[sizeof(packet) - 1] ^= 1;
        leftIp.Send(RightIP, 1, packet, sizeof(packet));
        packet[sizeof(packet) - 1] ^= 1;
        leftIp.Send(RightIP, 1, packet, sizeof(packet));
        for (unsigned i = 0; i < 1000000 && client.replies < 2; ++i) asm volatile("pause");
        Require(client.replies == 2, "ICMP validated odd payload and discarded invalid checksum");
        Require(server.requests == 3 && server.badChecksum == 1, "ICMP receiver saw the three wire requests");
        printf((char*)"PCNET IPV4 ICMP PASS\n");
    }
}

extern "C" void NativeProcessSmoke(void*, unsigned magic) {
    Require(magic == 0x2badb002, "PCnet Multiboot");
    GlobalDescriptorTable gdt;
    InterruptsManager interrupts(0x20, &gdt, nullptr);
    PeripheralComponentInterconnectController pci;
    auto leftDescriptor = FindAdapter(pci, 5);
    auto rightDescriptor = FindAdapter(pci, 6);
    MemoryManager memory(reinterpret_cast<uint32_t>(driverMemory), sizeof(driverMemory));
    auto badDevice = leftDescriptor;
    badDevice.portBase = 0;
    Require(!pci.GetDriver(badDevice, &interrupts), "PCnet missing BAR rejected");
    badDevice.portBase = 0xfff0;
    Require(!pci.GetDriver(badDevice, &interrupts), "PCnet I/O range rejected");
    badDevice = leftDescriptor;
    badDevice.interrupt = 0xff;
    Require(!pci.GetDriver(badDevice, &interrupts), "PCnet invalid IRQ rejected");
    auto* leftDriver = pci.GetDriver(leftDescriptor, &interrupts);
    auto* rightDriver = pci.GetDriver(rightDescriptor, &interrupts);
    Require(leftDriver && rightDriver, "PCnet driver discovery");
    auto& left = *static_cast<amd_am79c973*>(leftDriver);
    auto& right = *static_cast<amd_am79c973*>(rightDriver);
    Require(left.GetMACAddress() == 0x010000000002ULL, "PCnet left MAC");
    Require(right.GetMACAddress() == 0x020000000002ULL, "PCnet right MAC");
    left.SetIPAddress(0x0100010a);
    left.Activate();
    right.Activate();
    // SeaBIOS may route both adapters to one PIC line. Exercise both handlers
    // through that line and let each adapter check its own pending bit.
    const unsigned leftVector = leftDescriptor.interrupt + interrupts.HardwareInterruptOffset();
    NetworkInterrupt leftIrq(interrupts, leftVector, left,
        leftDescriptor.interrupt == rightDescriptor.interrupt ? &right : nullptr);
    Unmask(leftDescriptor.interrupt);
    Unmask(rightDescriptor.interrupt);
    interrupts.Activate();

    {
        Receiver leftReceive(left, right.GetMACAddress());
        Receiver rightReceive(right, left.GetMACAddress());
        uint8_t invalid[1515] = {};
        left.Send(nullptr, 60);
        left.Send(invalid, -1);
        left.Send(invalid, 13);
        left.Send(invalid, sizeof(invalid));
        for (unsigned start = 0; start < 4 * BurstSize; start += BurstSize) {
            SendBurst(left, right.GetMACAddress(), start);
            WaitFor(rightReceive, start + BurstSize);
            SendBurst(right, left.GetMACAddress(), start);
            WaitFor(leftReceive, start + BurstSize);
            if (start == BurstSize) {
                memory::InterruptGuard guard;
                left.Activate();
                right.Activate();
                interrupts.Load(&leftIrq, leftVector);
                Require(left.GetIPAddress() == 0x0100010a, "PCnet IP state survives hardware reinitialization");
            }
        }
        printf((char*)"PCNET FRAME COUNTS left=");
        printfHex32(leftReceive.received);
        printf((char*)" right=");
        printfHex32(rightReceive.received);
        printf((char*)"\n");
    }
    VerifyIpv4(left, right);
    Finish(true);
}
