#define NativeProcessSmoke UnusedPcnetSmoke
#include "pcnet_smoke.cpp"
#undef NativeProcessSmoke
#include <net/tcp_client.h>
#include <process/native_tcp.h>

extern "C" uint8_t kernel_start, kernel_end, kernel_readonly_start, kernel_readonly_end;

namespace {
    const uint32_t LocalIP = 0x0f02000a;
    const uint32_t FixtureIP = 0x6402000a;
    const unsigned BodyBytes = 65573;
    const char RequestLine[] = "GET /network-proof HTTP/1.0\r\nHost: gtos.test\r\nX-Pad: ";
    const char ResponseHeader[] = "HTTP/1.0 200 OK\r\nContent-Length: 65573\r\n\r\n";
    constexpr unsigned HeaderBytes = sizeof(ResponseHeader) - 1;
    constexpr unsigned ResponseBytes = HeaderBytes + BodyBytes;

    uint8_t ResponseByte(unsigned index) {
        if (index < HeaderBytes) return static_cast<uint8_t>(ResponseHeader[index]);
        const unsigned offset = index - HeaderBytes;
        return static_cast<uint8_t>(offset * 17 + (offset >> 8) * 7);
    }

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

    void WaitTick(TcpClient& client, const Timer& timer, uint32_t deadline = 20000) {
        Require(timer.milliseconds < deadline, "TCP smoke deadline");
        client.Poll(timer.milliseconds);
        asm volatile("hlt");
    }

    constexpr unsigned Owner = 1, OtherOwner = 2;
    constexpr unsigned RequestAddress = memory::ProcessAddressSpace::UserBase;
    constexpr unsigned ResultAddress = RequestAddress + 4096;

    template<class Request>
    int Invoke(process::NativeTcp& endpoint, unsigned owner, memory::ProcessAddressSpace& space,
               unsigned operation, const Request& request) {
        Require(space.CopyToUser(RequestAddress, &request, sizeof(request)), "TCP ABI request copy");
        return endpoint.Call(owner, space, operation, RequestAddress, sizeof(request));
    }

    void CheckNativeEndpoint(void* boot, unsigned magic, TcpClient& client, const Timer& timer,
                             const uint8_t* request, unsigned requestBytes) {
        static memory::PhysicalMemoryManager frames;
        static memory::KernelPaging paging;
        static memory::ProcessAddressSpace first, second;
        Require(frames.initialize(boot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end),
                "TCP ABI frame allocator");
        const memory::PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
            (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end,
            static_cast<const memory::MultibootInfo*>(boot), 0, 0};
        {
            memory::InterruptGuard interrupts;
            Require(paging.prepareIdentity(frames, config), "TCP ABI prepare paging");
            Require(paging.enable(), "TCP ABI enable paging with interrupts disabled");
            Require(paging.sealForSharedProcessors(), "TCP ABI seal shared paging");
        }
        const auto baseline = frames.getStatistics().freeFrames;
        memory::ProcessAddressSpace* spaces[] = {&first, &second};
        for (auto* space : spaces) {
            Require(space->Prepare(paging, frames) && space->MapNewRange(RequestAddress, 8192, true) &&
                    space->Seal(), "TCP ABI separate address spaces");
        }
        TaskManager scheduler;
        process::NativeTcp endpoint(client, scheduler, LocalIP, FixtureIP, 8080, 17);
        GtosTcpConnectRequest connect{GTOS_TCP_ABI_VERSION, FixtureIP, 8081, 0};
        Require(Invoke(endpoint, Owner, first, GTOS_SYS_TCP_CONNECT, connect) == GTOS_TCP_ERR_DENIED &&
                !endpoint.ActiveSockets(), "TCP ABI denies unlisted port");
        connect = {GTOS_TCP_ABI_VERSION, FixtureIP + (1U << 24), 8080, 0};
        Require(Invoke(endpoint, Owner, first, GTOS_SYS_TCP_CONNECT, connect) == GTOS_TCP_ERR_DENIED,
                "TCP ABI denies unlisted address");
        connect = {GTOS_TCP_ABI_VERSION, FixtureIP, 8080, 1};
        Require(Invoke(endpoint, Owner, first, GTOS_SYS_TCP_CONNECT, connect) == GTOS_TCP_ERR_INVALID &&
                endpoint.Call(Owner, first, GTOS_SYS_TCP_CONNECT, 0, sizeof(connect)) == GTOS_TCP_ERR_BAD_ADDRESS &&
                endpoint.Call(Owner, first, GTOS_SYS_TCP_CONNECT, RequestAddress, sizeof(connect) - 1) ==
                    GTOS_TCP_ERR_INVALID, "TCP ABI bounds connect arguments");
        connect.flags = 0;
        const int handle = Invoke(endpoint, Owner, first, GTOS_SYS_TCP_CONNECT, connect);
        Require(handle > 0 && endpoint.ActiveSockets() == 1 && endpoint.OpenedSockets() == 1,
                "TCP ABI opens exact allowed endpoint");
        GtosTcpInfoRequest query{GTOS_TCP_ABI_VERSION, static_cast<unsigned>(handle), ResultAddress};
        GtosTcpInfo info{};
        Require(Invoke(endpoint, Owner, first, GTOS_SYS_TCP_INFO, query) == 0 &&
                first.CopyFromUser(&info, ResultAddress, sizeof(info)) &&
                info.version == GTOS_TCP_ABI_VERSION && info.local_ipv4 == LocalIP &&
                info.peer_ipv4 == FixtureIP && info.local_port == 49152 && info.peer_port == 8080,
                "TCP ABI returns exact endpoints");
        const GtosTcpControlRequest close{GTOS_TCP_ABI_VERSION, static_cast<unsigned>(handle)};
        Require(Invoke(endpoint, OtherOwner, second, GTOS_SYS_TCP_INFO, query) == GTOS_TCP_ERR_BAD_HANDLE &&
                Invoke(endpoint, OtherOwner, second, GTOS_SYS_TCP_CLOSE, close) == GTOS_TCP_ERR_BAD_HANDLE,
                "TCP ABI refuses access and close from a second owner");
        query.result_va = ResultAddress + 4092;
        GtosTcpTransferRequest transfer{GTOS_TCP_ABI_VERSION, static_cast<unsigned>(handle),
            ResultAddress + 4092, 8};
        Require(Invoke(endpoint, Owner, first, GTOS_SYS_TCP_INFO, query) == GTOS_TCP_ERR_BAD_ADDRESS &&
                Invoke(endpoint, Owner, first, GTOS_SYS_TCP_READ, transfer) == GTOS_TCP_ERR_BAD_ADDRESS &&
                Invoke(endpoint, Owner, first, GTOS_SYS_TCP_WRITE, transfer) == GTOS_TCP_ERR_BAD_ADDRESS,
                "TCP ABI rejects transfers across an unmapped page");

        Require(first.CopyToUser(ResultAddress, request, requestBytes), "TCP ABI stage HTTP request");
        const uint32_t deadline = timer.milliseconds + 20000;
        unsigned sent = 0;
        while (sent < requestBytes) {
            transfer.buffer_va = ResultAddress + sent;
            transfer.bytes = requestBytes - sent;
            const int count = Invoke(endpoint, Owner, first, GTOS_SYS_TCP_WRITE, transfer);
            if (count == GTOS_TCP_ERR_AGAIN) {
                WaitTick(client, timer, deadline);
                continue;
            }
            Require(count > 0 && static_cast<unsigned>(count) <= transfer.bytes, "TCP ABI writes request prefix");
            sent += count;
        }
        query.result_va = ResultAddress;
        do {
            Require(Invoke(endpoint, Owner, first, GTOS_SYS_TCP_INFO, query) == 0 &&
                    first.CopyFromUser(&info, ResultAddress, sizeof(info)) && info.status == 0,
                    "TCP ABI observes queued response data");
            if (info.readable < 8) WaitTick(client, timer, deadline);
        } while (info.readable < 8);

        const uint32_t marker = 0x7182A3B4;
        uint32_t observed = 0;
        transfer.buffer_va = ResultAddress + 4092;
        transfer.bytes = 8;
        Require(first.CopyToUser(transfer.buffer_va, &marker, sizeof(marker)), "TCP ABI boundary marker");
        Require(Invoke(endpoint, Owner, first, GTOS_SYS_TCP_READ, transfer) == GTOS_TCP_ERR_BAD_ADDRESS &&
                first.CopyFromUser(&observed, transfer.buffer_va, sizeof(observed)) && observed == marker,
                "TCP ABI refuses an invalid read before writing the first page");
        uint8_t fragment[1024];
        transfer.buffer_va = ResultAddress;
        transfer.bytes = sizeof(fragment);
        unsigned received = 0;
        while (received < ResponseBytes) {
            const int count = Invoke(endpoint, Owner, first, GTOS_SYS_TCP_READ, transfer);
            if (count == GTOS_TCP_ERR_AGAIN) {
                WaitTick(client, timer, deadline);
                continue;
            }
            Require(count > 0 && static_cast<unsigned>(count) <= sizeof(fragment) &&
                    static_cast<unsigned>(count) <= ResponseBytes - received &&
                    first.CopyFromUser(fragment, ResultAddress, count), "TCP ABI reads response fragments");
            for (int i = 0; i < count; ++i, ++received)
                Require(fragment[i] == ResponseByte(received), "TCP ABI response bytes after rejected read");
        }
        printf((char*)"TCP NATIVE TRANSFERS VERIFIED\n");
        endpoint.ReclaimOwner(OtherOwner);
        Require(endpoint.ActiveSockets() == 1, "TCP ABI preserves another owner's handle");
        endpoint.ReclaimOwner(Owner);
        Require(!endpoint.ActiveSockets() &&
                Invoke(endpoint, Owner, first, GTOS_SYS_TCP_CLOSE, close) == GTOS_TCP_ERR_BAD_HANDLE,
                "TCP ABI reclaims only the stopped owner's handle");
        Require(first.Destroy() && second.Destroy() && frames.getStatistics().freeFrames == baseline,
                "TCP ABI address-space teardown restores baseline");
        printf((char*)"TCP NATIVE ENDPOINT PASS\n");
    }
}

extern "C" void NativeProcessSmoke(void* boot, unsigned magic) {
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
    Require(ip.IsOnLink(FixtureIP) && !ip.IsOnLink(0x016433c6), "TCP distinguishes directly connected peers");
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

    // Receive a burst larger than the old window without draining the ring.
    unsigned buffered = 0;
    bool eof = false;
    while (buffered < 8192) {
        Require(client.Describe(handle, buffered, eof) == 0 && !eof,
                "TCP receives a burst before the application reads");
        if (buffered < 8192) WaitTick(client, timer);
    }

    uint8_t fragment[379];
    unsigned received = 0;
    while (received < ResponseBytes) {
        const int count = client.Read(handle, fragment, sizeof(fragment));
        if (count == TcpClient::Again) {
            WaitTick(client, timer);
            continue;
        }
        Require(count > 0, "TCP read completes before EOF");
        for (int i = 0; i < count; ++i, ++received) {
            Require(received < ResponseBytes, "TCP response length");
            Require(fragment[i] == ResponseByte(received), "TCP response bytes");
        }
    }
    printf((char*)"TCP HTTP RESPONSE VERIFIED\n");
    int shutdown;
    while ((shutdown = client.ShutdownWrite(handle)) == TcpClient::Again) WaitTick(client, timer);
    Require(shutdown == 0, "TCP peer acknowledged local FIN");
    Require(client.Write(handle, request, 1) == TcpClient::Closed, "TCP cannot write after shutdown");
    Require(client.Close(handle) && !client.Close(handle) && client.Status(handle) == TcpClient::Invalid,
            "TCP released its own handle once");
    CheckNativeEndpoint(boot, magic, client, timer, request, sizeof(request));
    printf((char*)"PCNET TCP HTTP PASS\n");
    Finish(true);
}
