#define NativeProcessSmoke UnusedPcnetSmoke
#include "pcnet_smoke.cpp"
#undef NativeProcessSmoke
#include <net/tcp_client.h>

namespace {
    const uint32_t LocalIP = 0x0f02000a;
    const uint32_t FixtureIP = 0x6402000a;
    const unsigned BodyBytes = 6917;
    const char RequestLine[] = "GET /network-proof HTTP/1.0\r\nHost: gtos.test\r\nX-Pad: ";
    const char ResponseHeader[] = "HTTP/1.0 200 OK\r\nContent-Length: 6917\r\n\r\n";

    class Timer : public InterruptHandler {
    public:
        volatile uint32_t milliseconds;

        explicit Timer(InterruptsManager& interrupts)
        : InterruptHandler(&interrupts, 0x20), milliseconds(0) {
            Port8Bit control(0x43), channel(0x40);
            control.Write(0x36);
            channel.Write(1193 & 255);
            channel.Write(1193 >> 8);
        }

        uint32_t HandlerInterrupt(uint32_t esp) {
            ++milliseconds;
            return esp;
        }
    };

    void WaitTick(TcpClient& client, const Timer& timer) {
        Require(timer.milliseconds < 20000, "TCP smoke deadline");
        client.Poll(timer.milliseconds);
        asm volatile("hlt");
    }
}

extern "C" void NativeProcessSmoke(void*, unsigned magic) {
    Require(magic == 0x2badb002, "TCP Multiboot");
    GlobalDescriptorTable gdt;
    InterruptsManager interrupts(0x20, &gdt, nullptr);
    PeripheralComponentInterconnectController pci;
    auto descriptor = FindAdapter(pci, 5);
    MemoryManager memory(reinterpret_cast<uint32_t>(driverMemory), sizeof(driverMemory));
    auto* driver = pci.GetDriver(descriptor, &interrupts);
    Require(driver != nullptr, "TCP PCnet driver discovery");
    auto& adapter = *static_cast<amd_am79c973*>(driver);
    adapter.SetIPAddress(LocalIP);
    EtherFrameProvider link(&adapter);
    AddressResolutionProtocol arp(&link);
    InternetProtocolProvider ip(&link, &arp, 0, 0x00ffffff);
    TcpClient client(ip);
    Timer timer(interrupts);
    adapter.Activate();
    Unmask(descriptor.interrupt);
    interrupts.Activate();

    Require(client.Open(0x0100007f, 8080, 40000, 1) == TcpClient::Invalid, "TCP rejects loopback through NIC");
    client.Poll(timer.milliseconds);
    const uint32_t started = timer.milliseconds;
    // Force the 32-bit sequence number to wrap during the request.
    const int handle = client.Open(FixtureIP, 8080, 40000, 0xffffffccU);
    Require(handle > 0, "TCP opens a bounded slot");
    Require(client.Open(FixtureIP, 8080, 40000, 2) == TcpClient::Invalid, "TCP rejects port reuse");
    while (client.Status(handle) == TcpClient::Again) WaitTick(client, timer);
    const int status = client.Status(handle);
    if (status != 0) {
        printf((char*)"TCP CONNECT ERROR ");
        printfHex32(static_cast<uint32_t>(status));
        printf((char*)"\n");
        Require(client.Close(handle), "TCP released unsuccessful connect");
        Finish(false);
    }
    Require(timer.milliseconds - started >= 250, "TCP connects after cold ARP retry");
    printf((char*)"TCP CONNECTED AFTER ARP\n");

    uint8_t request[sizeof(RequestLine) - 1 + 687 + 4];
    for (unsigned i = 0; i < sizeof(RequestLine) - 1; ++i) request[i] = RequestLine[i];
    for (unsigned i = sizeof(RequestLine) - 1; i < sizeof(request) - 4; ++i) request[i] = 'x';
    request[sizeof(request) - 4] = request[sizeof(request) - 2] = '\r';
    request[sizeof(request) - 3] = request[sizeof(request) - 1] = '\n';
    unsigned sent = 0, writeCalls = 0;
    while (sent < sizeof(request)) {
        const int count = client.Write(handle, request + sent, sizeof(request) - sent);
        if (count == TcpClient::Again) {
            WaitTick(client, timer);
            continue;
        }
        Require(count > 0 && static_cast<unsigned>(count) <= sizeof(request) - sent, "TCP accepted a write prefix");
        sent += static_cast<unsigned>(count);
        ++writeCalls;
    }
    Require(writeCalls >= 2, "TCP divided the request by maximum segment size");

    uint8_t fragment[379];
    unsigned received = 0;
    const unsigned expectedBytes = sizeof(ResponseHeader) - 1 + BodyBytes;
    while (received < expectedBytes) {
        const int count = client.Read(handle, fragment, sizeof(fragment));
        if (count == TcpClient::Again) {
            WaitTick(client, timer);
            continue;
        }
        Require(count > 0, "TCP read completes before EOF");
        for (int i = 0; i < count; ++i, ++received) {
            Require(received < expectedBytes, "TCP response length");
            const uint8_t expected = received < sizeof(ResponseHeader) - 1
                ? static_cast<uint8_t>(ResponseHeader[received])
                : static_cast<uint8_t>((received - sizeof(ResponseHeader) + 1) * 17);
            Require(fragment[i] == expected, "TCP response bytes");
        }
    }
    printf((char*)"TCP HTTP RESPONSE VERIFIED\n");
    int shutdown;
    while ((shutdown = client.ShutdownWrite(handle)) == TcpClient::Again) WaitTick(client, timer);
    Require(shutdown == 0, "TCP peer acknowledged local FIN");
    Require(client.Write(handle, request, 1) == TcpClient::Closed, "TCP cannot write after shutdown");
    Require(client.Close(handle) && !client.Close(handle) && client.Status(handle) == TcpClient::Invalid,
            "TCP released its own handle once");
    printf((char*)"PCNET TCP HTTP PASS\n");
    Finish(true);
}
