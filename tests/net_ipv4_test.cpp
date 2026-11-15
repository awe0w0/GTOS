// Exercise the real Ethernet and IPv4 receivers without hardware I/O.
#include <net/ipv4.h>

using namespace gtos::drivers;
using namespace gtos::hardwarecommunication;
using namespace gtos::net;

static uint8_t sentFrame[1514];
static unsigned transmitted;
static int sentBytes;

InterruptHandler::InterruptHandler(InterruptsManager* manager, uint8_t number)
    : interruptNumber(number), interruptManager(manager) {}
InterruptHandler::~InterruptHandler() {}
uint32_t InterruptHandler::HandlerInterrupt(uint32_t esp) { return esp; }

amd_am79c973::amd_am79c973(PeripheralComponentInterconnectDeviceDescriptor*, InterruptsManager*)
    : InterruptHandler(nullptr, 0), MACAddress0Port(0), MACAddress2Port(0), MACAddress4Port(0),
      registerDataPort(0), registerAddressPort(0), resetPort(0), busControlRegisterDataPort(0) {}
amd_am79c973::~amd_am79c973() {}
void amd_am79c973::Activate() {}
int amd_am79c973::Reset() { return 0; }
uint32_t amd_am79c973::HandlerInterrupt(uint32_t esp) { return esp; }
uint32_t amd_am79c973::GetIPAddress() { return 0x020200C0U; }
uint64_t amd_am79c973::GetMACAddress() { return 0x060504030202ULL; }
void amd_am79c973::Send(uint8_t* frame, int size) {
    ++transmitted;
    sentBytes = size;
    if (size <= (int)sizeof(sentFrame))
        for (int i = 0; i < size; ++i) sentFrame[i] = frame[i];
}

RawDataHandler::RawDataHandler(amd_am79c973* device) : backend(device) {}
RawDataHandler::~RawDataHandler() {}
bool RawDataHandler::OnRawDataReceived(uint8_t*, uint32_t) { return false; }

namespace {
    constexpr uint32_t ethernetBytes = 14;
    constexpr uint32_t sourceAddress = 0x010200C0U;
    constexpr uint32_t localAddress = 0x020200C0U;
    constexpr uint32_t payloadBytes = 7;
    unsigned failures;

    void Print(const char* text) {
        uint32_t length = 0, result = 4;
        while (text[length]) ++length;
        asm volatile("int $0x80" : "+a"(result) : "b"(1), "c"(text), "d"(length) : "cc", "memory");
    }
    void Check(bool pass, const char* name) {
        if (!pass) {
            ++failures;
            Print("FAIL IPv4: ");
            Print(name);
            Print("\n");
        }
    }

    // Reference sums the bounded fixture's big-endian words and folds once at the end.
    uint16_t WireChecksum(const uint8_t* bytes, uint32_t length) {
        uint32_t sum = 0;
        for (uint32_t i = 0; i < length; ++i) sum += uint32_t(bytes[i]) << ((i % 2) ? 0 : 8);
        return 0xFFFFU - (sum % 0xFFFFU ? sum % 0xFFFFU : (sum ? 0xFFFFU : 0));
    }

    struct Frame {
        uint8_t bytes[ethernetBytes + 48] = {};
        explicit Frame(uint8_t words = 5) {
            const uint8_t header[] = {
                2, 2, 3, 4, 5, 6, 2, 7, 8, 9, 10, 11, 0x08, 0x00,
                uint8_t(0x40 | words), 0, 0, uint8_t(4 * words + payloadBytes),
                0x12, 0x34, 0x40, 0, 1, 17, 0, 0, 192, 0, 2, 1, 192, 0, 2, 2,
            };
            for (uint32_t i = 0; i < sizeof(header); ++i) bytes[i] = header[i];
            if (words == 6) bytes[ethernetBytes + 20] = 1; // NOP, then end-of-options.
            for (uint32_t i = 0; i < payloadBytes; ++i) bytes[ethernetBytes + 4 * words + i] = 0x80 + i;
            Seal(4 * words);
        }
        uint8_t* IP() { return bytes + ethernetBytes; }
        void Seal(uint32_t headerBytes = 20) {
            IP()[10] = IP()[11] = 0;
            const uint16_t sum = WireChecksum(IP(), headerBytes);
            IP()[10] = sum >> 8;
            IP()[11] = sum & 0xFF;
        }
    };

    class Receiver : public InternetProtocolHandler {
    public:
        Receiver(InternetProtocolProvider* provider) : InternetProtocolHandler(provider, 17) {}
        bool OnInternetProtocolReceived(uint32_t src, uint32_t dst, uint8_t* data, uint32_t size) override {
            ++calls;
            if (src != sourceAddress || dst != localAddress || size != payloadBytes) {
                Check(false, "exact payload bounds and endpoints");
                return false;
            }
            for (uint32_t i = 0; i < size; ++i) Check(data[i] == 0x80 + i, "payload bytes after header");
            return reply;
        }
        unsigned calls = 0;
        bool reply = false;
    };

    void Refused(EtherFrameProvider& ethernet, Receiver& receiver, Frame& frame, uint32_t size,
                 const char* description) {
        const Frame before = frame;
        const unsigned calls = receiver.calls;
        Check(!ethernet.OnRawDataReceived(frame.bytes, size) && receiver.calls == calls, description);
        bool unchanged = true;
        for (uint32_t i = 0; i < sizeof(frame.bytes); ++i) unchanged &= frame.bytes[i] == before.bytes[i];
        Check(unchanged, "discard preserves input frame");
    }

    void CheckArp(EtherFrameProvider& ethernet, AddressResolutionProtocol& arp,
                  InternetProtocolProvider& ipv4) {
        const uint64_t peerMAC = 0x0B0A09080702ULL;
        const uint8_t reply[] = {
            2, 2, 3, 4, 5, 6, 2, 7, 8, 9, 10, 11, 0x08, 0x06,
            0, 1, 0x08, 0, 6, 4, 0, 2,
            2, 7, 8, 9, 10, 11, 192, 0, 2, 1,
            2, 2, 3, 4, 5, 6, 192, 0, 2, 2,
        };
        uint8_t frame[sizeof(reply)];
        for (unsigned i = 0; i < sizeof(reply); ++i) frame[i] = reply[i];
        Check(!ethernet.OnRawDataReceived(frame, sizeof(frame)), "unsolicited ARP does not transmit");
        uint64_t mac = 0;
        Check(!arp.Resolve(sourceAddress, mac) && !mac && transmitted == 1,
              "unresolved ARP sends once and returns without changing the output MAC");
        Check(sentBytes == (int)sizeof(reply) && sentFrame[0] == 0xFF && sentFrame[5] == 0xFF &&
              sentFrame[12] == 0x08 && sentFrame[13] == 0x06 && sentFrame[21] == 1 &&
              sentFrame[32] == 0 && sentFrame[37] == 0 && sentFrame[41] == 1,
              "wire request is a broadcast with an unfilled target hardware address");

        frame[32] ^= 2;
        Check(!ethernet.OnRawDataReceived(frame, sizeof(frame)) && !arp.Resolve(sourceAddress, mac) &&
              !mac && transmitted == 2, "reply addressed to another hardware address cannot resolve");
        frame[32] ^= 2;
        Check(!ethernet.OnRawDataReceived(frame, sizeof(frame) - 1) && !arp.Resolve(sourceAddress, mac) &&
              transmitted == 3, "truncated reply cannot resolve");
        Check(!ethernet.OnRawDataReceived(frame, sizeof(frame)) && arp.Resolve(sourceAddress, mac) &&
              mac == peerMAC && transmitted == 3, "valid solicited reply resolves the pending unicast address");
        frame[23] ^= 1;
        Check(!ethernet.OnRawDataReceived(frame, sizeof(frame)) && arp.Resolve(sourceAddress, mac) &&
              mac == peerMAC && transmitted == 3, "duplicate reply cannot replace a resolved mapping");
        Check(!arp.Resolve(0, mac) && !arp.Resolve(0x01E0U, mac) && transmitted == 3,
              "unspecified and multicast addresses produce no ARP traffic");

        uint8_t data = 0x5A;
        ipv4.Send(0x030200C0U, 17, &data, 1);
        Check(transmitted == 4 && sentFrame[13] == 0x06,
              "IPv4 waits for a neighbor before transmitting data");
        ipv4.Send(sourceAddress, 17, &data, 1);
        Check(transmitted == 5 && sentBytes == 35 && sentFrame[1] == 7 && sentFrame[13] == 0 &&
              sentFrame[34] == data && WireChecksum(sentFrame + ethernetBytes, 20) == 0,
              "IPv4 sends a valid frame once the route resolves");

        frame[23] ^= 1;
        frame[21] = 1;
        Check(ethernet.OnRawDataReceived(frame, sizeof(frame)) && frame[21] == 2 && frame[1] == 7 &&
              frame[23] == 2 && frame[31] == 2 && frame[41] == 1,
              "a request for the local address produces an ARP reply");
    }
}

extern "C" int NetIpv4TestMain() {
    amd_am79c973 device(nullptr, nullptr);
    EtherFrameProvider ethernet(&device);
    alignas(16) uint8_t heap[4096];
    gtos::MemoryManager memory(reinterpret_cast<size_t>(heap), sizeof(heap));
    AddressResolutionProtocol arp(&ethernet);
    InternetProtocolProvider ipv4(&ethernet, &arp, 0, 0x00FFFFFFU);
    Receiver receiver(&ipv4);
    Frame padded;
    Check(!ethernet.OnRawDataReceived(padded.bytes, sizeof(padded.bytes)) && receiver.calls == 1,
          "normal frame delivers decoded big-endian length without link padding");
    Frame options(6);
    Check(!ethernet.OnRawDataReceived(options.bytes, sizeof(options.bytes)) && receiver.calls == 2,
          "IPv4 options advance the payload start");
    Check(!ethernet.OnRawDataReceived(nullptr, 0), "missing Ethernet bytes");
    Refused(ethernet, receiver, padded, ethernetBytes + 19, "truncated IPv4 header");
    Refused(ethernet, receiver, padded, ethernetBytes + 26, "truncated IPv4 payload");
    Frame bad;
    bad.IP()[0] = 0x65; bad.Seal();
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "unsupported IP version");
    bad = Frame(); bad.IP()[0] = 0x44; bad.Seal();
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "undersized IHL");
    bad = Frame(6); bad.IP()[3] = 20; bad.Seal(24);
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "total length smaller than IHL");
    bad = Frame(); bad.IP()[6] = 0x20; bad.Seal();
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "fragment reassembly is unavailable");
    bad = Frame(); bad.IP()[7] = 1; bad.Seal();
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "nonzero fragment offset");
    bad = Frame(); bad.IP()[6] = 0x80; bad.Seal();
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "reserved fragment flag");
    bad = Frame(); bad.IP()[8] = 0; bad.Seal();
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "expired TTL");
    bad = Frame(); bad.IP()[10] ^= 1;
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "header checksum mismatch");
    bad = Frame(); bad.IP()[19] = 3; bad.Seal();
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "another host's IP packet");
    bad = Frame(); bad.IP()[9] = 255; bad.Seal();
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "unbound last protocol number");
    bad = Frame(); bad.bytes[12] = bad.bytes[13] = 255;
    Refused(ethernet, receiver, bad, sizeof(bad.bytes), "unbound last EtherType");

    receiver.reply = true;
    Check(ethernet.OnRawDataReceived(options.bytes, sizeof(options.bytes)) && receiver.calls == 3,
          "registered handler can request a reply");
    Check(options.bytes[1] == 7 && options.bytes[7] == 2 && options.IP()[15] == 2 &&
          options.IP()[19] == 1 && options.IP()[8] == 64 && WireChecksum(options.IP(), 24) == 0,
          "reply swaps endpoints and recomputes the full IPv4 header checksum");
    const uint8_t oddUnaligned[] = {0, 0x80, 0x10, 0xFE};
    Check(InternetProtocolProvider::Checksum(oddUnaligned + 1, 3) == 0xEE81U,
          "unsigned odd byte at an unaligned address");
    CheckArp(ethernet, arp, ipv4);
    if (!failures) Print("PASS IPv4 ingress bounds, checksum, fragments, replies and bounded ARP resolution\n");
    return failures ? 1 : 0;
}

asm(".global _start\n_start:\n andl $-16, %esp\n call NetIpv4TestMain\n"
    " movl %eax, %ebx\n movl $1, %eax\n int $0x80\n");
